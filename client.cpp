/**
 * ZeroMQ Client Implementation
 * 
 * This program creates a client that:
 * - Connects to a ZeroMQ server for network communication
 * - Sends commands and processes responses
 * - Uses secure communication with curve cryptography
 * - Supports file transfer operations (send/get)
 * - Maintains connection health through heartbeats
 */

#include <iostream>     // For input/output operations (cout, cerr)
#include <fstream>      // For file operations (ifstream, ofstream)
#include <filesystem>   // For filesystem operations
#include <random>       // For random number generation
#include <thread>       // For creating and managing threads
#include <zmq.hpp>      // ZeroMQ C++ binding for network communication

#include <string>       // For string handling
#include <vector>       // For dynamic arrays (vectors)
#include <chrono>       // For time-related functions
#include <vector>       // Redundant include

#include <atomic>       // For thread-safe variables
#include <condition_variable>  // For thread synchronization
#include <mutex>        // For thread synchronization

// Atomic variables for thread-safe state tracking across multiple threads
std::atomic<bool> heartbeat_state (true);    // Tracks if heartbeat communication is active
std::atomic<bool> connection_state (false);  // Tracks if connection to server is established
std::atomic<bool> abort_state (false);       // Flag to signal threads to terminate

/**
 * Helper function to check if a string ends with a specific suffix
 * 
 * @param str The string to check
 * @param suffix The suffix to look for
 * @return true if the string ends with the suffix, false otherwise
 */
bool ends_with_suffix(const std::string& str, const std::string& suffix) {
    if (str.length() >= 4) {
        return str.substr(str.length() - 4) == suffix;
    }
    return false;
}

/**
 * Handles command processing and communication with the server
 * 
 * @param server_pkey The server's public key for secure communication
 * @param client_pkey The client's public key
 * @param client_skey The client's secret key
 */
void command_thread(std::string server_pkey, std::string client_pkey, std::string client_skey) {
    const size_t chunk_size = 65536;  // 64KB chunks for file transfers
    
    // ZeroMQ setup
    zmq::context_t ctx;              // Create a ZeroMQ context (required for all sockets)
    zmq::socket_t command_sock (ctx, zmq::socket_type::req);  // Create a REQ (request) socket
    //command_sock.set(zmq::sockopt::sndtimeo, 10000);  // Commented out timeout settings
    //command_sock.set(zmq::sockopt::rcvtimeo, 10000);
    
    // Set up security for the socket
    command_sock.set(zmq::sockopt::curve_serverkey, server_pkey);  // Server's public key
    command_sock.set(zmq::sockopt::curve_publickey, client_pkey);  // Our public key
    command_sock.set(zmq::sockopt::curve_secretkey, client_skey);  // Our secret key
    command_sock.set(zmq::sockopt::linger, 1);  // Close immediately on disconnect
    command_sock.connect("tcp://localhost:5556");  // Connect to the server's command port
    
    std::string input;
    while (true) {
        // Check if we should terminate this thread
        if(abort_state.load()==true) {
            break;
        }
        
        // Get command from user input
        std::getline(std::cin, input);
        std::stringstream ss(input);  // Use stringstream to split input into words
        std::string arg;
        std::vector<std::string> args;  // Store each word as a separate argument
        while (ss >> arg) {
            args.push_back(arg);
        }

        // Validate user input before sending to server
        int num_args = args.size();
        std::string command;
        if (num_args > 0) {
            command = args[0];  // First argument is the command
            
            // Handle SEND command
            if ( command == "send") {
                if(num_args == 1) {  // Check if a filename was provided
                    std::cout << "Please provide a .bsz file" << std::endl;
                    continue;
                }
                if(!ends_with_suffix(args[1],"bsz")) {  // Check file extension
                    std::cout << "Only .bsz files can be sent" << std::endl;
                    continue;
                }
                std::cout << "Sending:" << args[1] << std::endl;
            } 
            // Handle GET command
            else if (command == "get") {
                if(num_args == 1) {  // Check if a filename was provided
                    std::cout << "Please provide image filename" << std::endl;
                    continue;
                }
            } 
            // Handle EXIT command
            else if (command == "exit") {
                std::cout << "now" << std::endl;
                break;  // Exit the command loop
            } 
            // Handle RENDER command
            else if (command == "render") {
                std::string compoundArg;
                if(num_args > 1) {
                    // Combine remaining args into a single string
                    for (size_t i = 1; i < args.size(); ++i) {
                        compoundArg += args[i];
                        if (i < args.size() - 1) {
                            compoundArg += " ";  // Add spaces between arguments
                        }
                    }
                    std::cout << compoundArg << std::endl;
                }
            } 
            // Handle HELLO command
            else if (command == "hello") {
                ;  // No special validation needed
            } 
            // Handle unknown commands
            else {
                std::cout << "unknown" << std::endl;
                continue;  // Skip sending to server
            }        
        }

        // Send validated command to server over encrypted socket
        zmq::message_t server_response;
        zmq::message_t msg_command(command);
        //>>>ZOUT  (This comment indicates sending data to the network)
        command_sock.send(msg_command, zmq::send_flags::none);  // Send command to server
        std::cout << "Sent: " << input.data() << std::endl;

        //ZIN<<<  (This comment indicates receiving data from the network)
        command_sock.recv(server_response, zmq::recv_flags::none);  // Wait for server response
        std::string response_str(static_cast<char*>(server_response.data()), server_response.size()-1);

        // Process server response
        if(response_str=="RDY") {  // Server indicates it's ready for further communication
            std::cout << "Server Readiness: " << response_str << std::endl;
            
            // Handle different command types
            if(input == "exit") {
                break;  // Exit command - terminate the thread
            // RENDER command
            } else if(command == "render") {
                //>>>ZOUT
                command_sock.send(zmq::message_t("render"), zmq::send_flags::none);  // Send render command again
                //ZIN<<<
                command_sock.recv(server_response, zmq::recv_flags::none);  // Get final acknowledgment
            // STAT command - get status from server
            } else if(command == "stat") {
                //>>>ZOUT
                command_sock.send(zmq::message_t("stat"), zmq::send_flags::none);  // Request status
                //ZIN<<<
                command_sock.recv(server_response, zmq::recv_flags::none);  // Receive status data

            // GET command - download a file from server
            } else if(command == "get") {
                std::ofstream output_file("orange-juice.png", std::ios::binary);  // Open output file in binary mode
                std::cout << "getting\n";
                if (!output_file.is_open()) {
                    std::cerr << "Error opening file for writing" << std::endl;
                    std::cout << "ERR" << std::endl;
                    continue;  // Skip sending to server if we can't write to local file
                } else {
                    // File transfer loop - receive file in chunks
                    while (true) {
                        //>>>ZOUT
                        command_sock.send(zmq::message_t("GO"), zmq::send_flags::none);  // Request next chunk
                        zmq::message_t recv_data;
                        //ZIN<<<
                        command_sock.recv(recv_data, zmq::recv_flags::none);  // Receive chunk or status

                        // Check if we received a status message instead of data
                        if (recv_data.size() < 8) {  // Small messages are likely status signals
                            std::string recv_string(static_cast<const char*>(recv_data.data()), recv_data.size()-1);
                            if (recv_string == "EOF") {  // End of file signal
                                std::cout << "EOF" << std::endl;
                                break;  // Done receiving file
                            } else if(recv_string == "ERR")  {  // Error signal (file not found, etc.)
                                std::cout << "ERR client read ACK" << std::endl;
                                break;  // Stop due to error
                            } else {
                                std::cout << "HUH" << recv_string << std::endl;  // Unexpected response
                                break;
                            }
                        }
                        
                        // Process binary data chunk (larger messages are assumed to be file data)
                        std::cout << recv_data.size() << std::endl;  // Print chunk size
                        output_file.write(static_cast<char*>(recv_data.data()), recv_data.size());  // Write to file
                    }
                    output_file.close();  // Close file when done
                }
            // SEND command - upload a file to server
            } else if(command == "send") {
                std::string read_file = "./orange-juice.bsz";  // Hardcoded filename to send
                std::cout << "sending\n";
                std::ifstream binaryInputFile;
                binaryInputFile.open(read_file, std::ios::binary);  // Open file in binary mode
                if (!binaryInputFile.is_open()) {
                    std::cerr << "Error opening file for read" << std::endl;
                    //>>>ZOUT
                    command_sock.send(zmq::message_t("ERR"), zmq::send_flags::none);  // Signal error to server
                    ///ZIN<<<
                    command_sock.recv(server_response, zmq::recv_flags::none);  // Get acknowledgment
                } else {
                    // File sending loop - send file in chunks
                    std::vector<char> send_buffer(chunk_size);  // Buffer for file chunks
                    std::streamsize bytes_read_in_chunk;
                    while (true) {
                        binaryInputFile.read(send_buffer.data(), chunk_size);  // Read chunk from file
                        bytes_read_in_chunk = binaryInputFile.gcount();  // Get actual bytes read
                        if(bytes_read_in_chunk > 0){  // If we read data
                            zmq::message_t message(send_buffer.data(), bytes_read_in_chunk);  // Create message with data
                            //>>>ZOUT 
                            command_sock.send(message, zmq::send_flags::none);  // Send chunk to server
                            //ZIN<<<
                            command_sock.recv(server_response, zmq::recv_flags::none);  // Get acknowledgment
                        } else {
                            break;  // Exit when file is fully read
                        }
                    }
                    //<<<ZOUT
                    command_sock.send(zmq::message_t("EOF"), zmq::send_flags::none);  // Signal end of file
                    //ZIN>>>
                    command_sock.recv(server_response, zmq::recv_flags::none);  // Get final acknowledgment
                }
            }
        } else {
            // For simple commands, just print the server's response
            std::cout << "Server response: " << response_str << std::endl;
        }
    }
    command_sock.close();  // Clean up socket
    ctx.close();           // Clean up context
}

/**
 * Maintains a heartbeat connection with the server to detect disconnections
 * 
 * @param server_pkey The server's public key for secure communication
 * @param client_pkey The client's public key
 * @param client_skey The client's secret key
 */
void heartbeat_thread(std::string server_pkey, std::string client_pkey, std::string client_skey) {
    // ZeroMQ setup
    zmq::context_t ctx;
    zmq::socket_t heartbeat_sock (ctx, zmq::socket_type::req);  // Create REQ socket for heartbeats
    
    // Set up security for the socket
    heartbeat_sock.set(zmq::sockopt::curve_serverkey, server_pkey);  // Server's public key
    heartbeat_sock.set(zmq::sockopt::curve_publickey, client_pkey);  // Our public key
    heartbeat_sock.set(zmq::sockopt::curve_secretkey, client_skey);  // Our secret key
    heartbeat_sock.set(zmq::sockopt::linger, 1);  // Close immediately on disconnect
    heartbeat_sock.connect("tcp://localhost:5555");  // Connect to server's heartbeat port
    
    int heartbeat_count = 0;
    std::vector<zmq::pollitem_t> items = {};
    
    while (true) {
        // Check if we should terminate this thread
        if(abort_state.load()==true) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Short delay between heartbeats

        // Only send heartbeats if connection is established
        if(connection_state == true) {
            // Send heartbeat to server
            heartbeat_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);
            
            // Wait for response with timeout
            zmq::pollitem_t response_item = { heartbeat_sock, 0, ZMQ_POLLIN, 0 };
            zmq::poll(&response_item, 1, 100);  // Poll with 100ms timeout
            
            if (response_item.revents & ZMQ_POLLIN) {  // If we got a response
                zmq::message_t msg_response;
                heartbeat_sock.recv(msg_response, zmq::recv_flags::none);  // Receive it
                //std::cout << "Heartbeat Response: " << std::endl;  // Commented out debug print
            } else {  // No response within timeout
                std::cout << "Bella Server is unavailable" << std::endl;
                heartbeat_state = false;  // Mark heartbeat as failed
                connection_state = false;  // Mark connection as down
                break;  // Exit heartbeat loop
            }
        }
    }
    heartbeat_sock.close();  // Clean up socket
    ctx.close();             // Clean up context
}

/**
 * Retrieves the server's public key to establish a secure connection
 * 
 * @return The server's public key as a string
 */
std::string get_pubkey_from_srv() {
    // Note: This initial connection is not encrypted, but subsequent connections will be
    // ZeroMQ will establish perfect forward secrecy after initial handshake
    zmq::context_t ctx;
    zmq::socket_t pubkey_sock(ctx, zmq::socket_type::req);  // Create REQ socket
    pubkey_sock.connect("tcp://127.0.0.1:9555");  // Connect to server's key exchange port
    
    // Prepare authentication message with passphrase
    zmq::message_t z_out(std::string("Bellarender123"));

    // Send the passphrase to request the public key
    try {
        zmq::send_result_t send_result = pubkey_sock.send(z_out, zmq::send_flags::none);
    } catch (const zmq::error_t& e) {
        std::cout << "ERROR" << std::endl;
    }

    std::cout << "bellazmq connecting to server..." << std::endl;
    
    // Receive the server's public key
    zmq::message_t z_in;
    pubkey_sock.recv(z_in);
    std::string pub_key = z_in.to_string();
    
    // Clean up resources
    pubkey_sock.close();
    ctx.close();
    
    std::cout << "connection successful" << std::endl;
    connection_state = true;  // Mark connection as established
    return pub_key;
}

/**
 * Main function - program entry point
 * Sets up security, creates threads, and manages the overall connection
 */
int main()
{
    const size_t chunk_size = 32768;  // 32KB chunk size (not used in main)
    
    // Generate a unique cryptographic keypair for this client
    char client_skey[41] = { 0 };  // Secret key buffer (private key)
    char client_pkey[41] = { 0 };  // Public key buffer
    if ( zmq_curve_keypair(&client_pkey[0], &client_skey[0])) {  // Generate the keypair
        // 1 is failure
        std::cout << "\ncurve keypair gen failed.";
        exit(EXIT_FAILURE);  // Exit program if key generation fails
    }

    // Get server's public key to establish secure connection
    std::string server_pkey = get_pubkey_from_srv();
    /*if(server_pkey.empty()) {  // Commented out error handling
        std::cout << "Server is Down" << std::endl;
        heartbeat_state = false;
    }*/

    // Convert char arrays to strings for easier handling
    std::string client_pkey_str(client_pkey);
    std::string client_skey_str(client_skey);

    // Start worker threads for commands and heartbeats
    std::thread command_t(command_thread, server_pkey, client_pkey_str, client_skey_str);
    std::thread heartbeat_t(heartbeat_thread, server_pkey, client_pkey_str, client_skey_str);

    // Main monitoring loop - checks connection health
    while (true) {
        if (!heartbeat_state.load()) {  // If heartbeat has failed
            std::cout << "Dead" << std::endl;
            abort_state = true;  // Signal threads to terminate (note: this is using assignment, not comparison)
            break;
        }
        if (connection_state.load() == false) {  // If connection is down
            std::cout << "Dead2" << std::endl;
            abort_state = true;  // Signal threads to terminate (note: this is using assignment, not comparison)
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));  // Check every half-second
    }
    
    abort_state = true;  // Signal threads to terminate (note: this is using assignment, not comparison)
    command_t.join();    // Wait for command thread to finish
    heartbeat_t.join();  // Wait for heartbeat thread to finish
    return 0;
}

