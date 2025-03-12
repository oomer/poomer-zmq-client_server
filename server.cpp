/**
 * ZeroMQ Server Implementation
 * 
 * This program creates a server that:
 * - Uses ZeroMQ (zmq) for network communication
 * - Handles commands from clients through a command socket
 * - Monitors client connections through a heartbeat socket
 * - Provides secure communication with curve cryptography
 * - Supports file transfer operations (send/get)
 */

#include <iostream>     // For input/output operations (cout, cerr)
#include <fstream>      // For file operations (ifstream, ofstream)
#include <thread>       // For creating and managing threads
#include <zmq.hpp>      // ZeroMQ C++ binding for network communication
#include <vector>       // For dynamic arrays (vectors)
#include <chrono>       // For time-related functions

#include <sstream>      // For string stream operations
#include <atomic>       // For thread-safe variables

#include <thread>       // Already included above, redundant

// Atomic variables for thread-safe state tracking across multiple threads
std::atomic<bool> heartbeat_state (true);    // Tracks if heartbeat is active
std::atomic<bool> client_state (false);      // Tracks if a client is connected

/**
 * Handles all client commands on a separate thread
 * 
 * @param server_skey The server's secret key for secure communication
 */
void command_thread(std::string server_skey) {

    // ZeroMQ setup
    zmq::context_t ctx;                                // Create a ZeroMQ context (required for all sockets)
    zmq::socket_t command_sock(ctx, zmq::socket_type::rep);  // Create a REP (reply) socket
    //command_sock.set(zmq::sockopt::sndtimeo, 10000);  // Commented out timeout settings
    //command_sock.set(zmq::sockopt::rcvtimeo, 10000);
    command_sock.set(zmq::sockopt::curve_server, true);      // Enable secure curve encryption
    command_sock.set(zmq::sockopt::curve_secretkey, server_skey);  // Set the server's secret key
    //command_sock.set(zmq::sockopt::linger, 100);      // Commented out linger option
    command_sock.bind("tcp://*:5556");                       // Bind socket to port 5556 on all interfaces
    zmq::message_t client_response; 

    try {
        // File paths for operations
        std::string write_file = "./oomer.bsz";            
        std::string read_file = "./oomer.png";            
        const size_t chunk_size = 65536;                     // 64KB chunks for file transfers
        std::vector<char> sftp_buffer(chunk_size);           // Buffer to hold each chunk
        std::ofstream binaryOutputFile;                      // File stream for writing
        std::ifstream binaryInputFile;                       // File stream for reading
        
        // Main command processing loop
        while (true) {
            zmq::message_t msg_command; 
            //ZIN<<<  (This comment indicates receiving data from the network)
            command_sock.recv(msg_command, zmq::recv_flags::none);  // Wait for a command from client
            std::string client_command = msg_command.to_string();   // Convert message to string
            std::cout << "Command: " << client_command << std::endl;

            // Process different commands
            if(client_command == "hello"){  // Basic hello/bye command
                std::cout << "bye" << std::endl;
                //>>>ZOUT  (This comment indicates sending data to the network)
                command_sock.send(zmq::message_t("bye"), zmq::send_flags::none); 
            } else if (client_command == "exit") {  // Exit command
                std::cout << "exit" << std::endl;
                //>>>ZOUT
                command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);  // Send acknowledgment
            // RENDER command
            } else if (client_command == "render") {
                //engine.scene().read("./oomer.bsz");  // Commented out rendering code
                //engine.scene().camera()["resolution"] = Vec2 {200, 200};
                //engine.start();
                std::cout << "start render" << std::endl;
                //>>>ZOUT
                command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none); 

            //GET command - sends a file to the client
            } else if (client_command == "get") { //REP mode
                std::string read_file = "./oomer.png";
                std::cout << "Executing get command\n";
                std::ifstream binaryInputFile;
                binaryInputFile.open(read_file, std::ios::binary);  // Open file in binary mode
                if (!binaryInputFile.is_open()) {
                    std::cerr << "Error opening file for read" << std::endl;
                    //>>>ZOUT
                    command_sock.send(zmq::message_t("ERR"), zmq::send_flags::none);  // Send error if file can't be opened
                } else {
                    //>>>ZOUT
                    command_sock.send(zmq::message_t("RDY"), zmq::send_flags::none);  // Tell client we're ready to send
                    std::vector<char> send_buffer(chunk_size);
                    std::streamsize bytes_read_in_chunk;
                    while (true) {
                        zmq::message_t z_in;
                        //ZIN
                        command_sock.recv(z_in);  // Wait for client to request next chunk
                        binaryInputFile.read(send_buffer.data(), chunk_size);  // Read file chunk into buffer
                        bytes_read_in_chunk = binaryInputFile.gcount();  // Get actual bytes read
                        if(bytes_read_in_chunk > 0){
                            std::cout << bytes_read_in_chunk << std::endl;
                            zmq::message_t message(send_buffer.data(), bytes_read_in_chunk);  // Create message with chunk data
                            //ZOUT
                            command_sock.send(message, zmq::send_flags::none);  // Send chunk to client
                        } else {
                            //ZOUT
                            command_sock.send(zmq::message_t("EOF"), zmq::send_flags::none);  // Signal end of file
                            std::cout << "EOF" << std::endl;
                            break;  // Exit when file is fully sent
                        }
                    }
                }

            // STAT command - read and send log file contents
            } else if (client_command == "stat") {
                std::ifstream log_file("logfile.txt");
                if (log_file.is_open()) {
                    std::string log_line;
                    if (std::getline(log_file, log_line)) {  // Read a line from the log file
                        //>>>ZOUT
                        command_sock.send(zmq::message_t(log_line), zmq::send_flags::none);  // Send log line to client
                    } else {
                        //>>>ZOUT
                        command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);  // Empty log file
                    }
                    log_file.close();
                }
            // SEND command - receive a file from client
            } else if (client_command == "send") {
                std::ofstream output_file("oomer.bsz", std::ios::binary);  // Open file for writing in binary mode
                if (!output_file.is_open()) {
                    std::cerr << "Error opening file for writing" << std::endl;
                    //>>>ZOUT
                    command_sock.send(zmq::message_t("ERR"), zmq::send_flags::none);  // Send error if file can't be opened
                } else {  // File handle open and ready
                    //>>>ZOUT
                    command_sock.send(zmq::message_t("RDY"), zmq::send_flags::none);  // Tell client we're ready to receive
                    while (true) {
                        zmq::message_t recv_data;
                        //ZIN<<<
                        command_sock.recv(recv_data, zmq::recv_flags::none);  // Receive chunk from client
                        if(recv_data.size() < 8) {  // Check if this is a signal rather than data
                            // Small messages (under 8 bytes) are likely signals
                            std::string response_str(static_cast<char*>(recv_data.data()), recv_data.size()-1);
                            if (response_str=="EOF") {  // End of file signal
                                //>>>ZOUT
                                command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);
                                break;  // Done receiving file
                            } else if(response_str=="ERR")  {  // Error signal
                                std::cout << "ERR on client" << std::endl;
                                //>>>ZOUT
                                command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);
                                break;  // End transfer due to error
                            }
                        }
                        // Write received data to file
                        output_file.write(static_cast<char*>(recv_data.data()), recv_data.size());
                        //>>ZOUT
                        command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);  // Acknowledge chunk received
                    }
                    output_file.close();
                }
            } else {  // Unknown command
                //>>ZOUT
                command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);  // Still need to respond in REQ-REP pattern
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Small delay between commands
        }

    } catch (const zmq::error_t& e) {
        // Handle ZeroMQ-specific exceptions
        std::cerr << "ZMQ error: " << e.what() << std::endl;
        ctx.close();
        command_sock.close();
        //Potentially close sockets, clean up etc.
    } catch (const std::exception& e) {
        // Handle standard exceptions (e.g., std::bad_alloc)
        std::cerr << "Standard exception: " << e.what() << std::endl;
    } catch (...) {
        // Catch any other exceptions
        std::cerr << "Unknown exception caught." << std::endl;
    }
    command_sock.close();  // Clean up socket
    ctx.close();           // Clean up context
}

/**
 * Monitors client connection through regular heartbeat messages
 * 
 * @param server_skey The server's secret key for secure communication
 */
void heartbeat_thread(std::string server_skey) {
    heartbeat_state = true;  // Initialize heartbeat as active
    std::cout << "new heartbeat_thread" << std::endl;
    
    // ZeroMQ setup
    zmq::context_t ctx;
    zmq::socket_t heartbeat_sock (ctx, zmq::socket_type::rep);  // Create a REP socket for heartbeats
    heartbeat_sock.set(zmq::sockopt::curve_server, true);       // Enable encryption
    heartbeat_sock.set(zmq::sockopt::curve_secretkey, server_skey);  // Set secret key
    heartbeat_sock.bind("tcp://*:5555");                        // Listen on port 5555
    
    while(true) {
        // Only start checking heartbeats once a client connects
        if (client_state == true) {
            // Set up polling to check for incoming messages with timeout
            zmq::pollitem_t response_item = { heartbeat_sock, 0, ZMQ_POLLIN, 0 };
            zmq::poll(&response_item, 1, 25000);  // Wait for heartbeat with 25 second timeout

            if (response_item.revents & ZMQ_POLLIN) {  // If we received a message
                zmq::message_t message;
                //ZIN<<<
                heartbeat_sock.recv(message, zmq::recv_flags::none);  // Receive heartbeat
                //ZOUT>>>
                heartbeat_sock.send(zmq::message_t("ACK"), zmq::send_flags::dontwait);  // Acknowledge without blocking
            } else {  // Timeout - no heartbeat received
                std::cout << "Bella Client Lost" << std::endl;
                heartbeat_state = false;  // Mark client as disconnected
            }
        } 
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Small delay between checks
    }
    heartbeat_sock.close();  // Clean up socket
    ctx.close();             // Clean up context
}

/**
 * Serves the public key to clients that request it
 * This is how clients get the key needed to establish a secure connection
 * 
 * @param pub_key The server's public key to share with clients
 */
void pkey_server(const std::string& pub_key) {
    zmq::context_t ctx;
    zmq::socket_t sock(ctx, zmq::socket_type::rep);  // Create REP socket
    sock.bind("tcp://*:9555");  // Bind to port 9555

    zmq::message_t z_in;
    std::cout << "Entered: Public Key Serving Mode" << std::endl; 
    //ZIN<<<
    sock.recv(z_in);  // Wait for client request
    
    // Check if the request contains the correct passphrase
    if (z_in.to_string().compare("Bellarender123") == 0) {
        zmq::message_t z_out(pub_key);  // Create message with the public key
        //ZOUT>>>
        sock.send(z_out, zmq::send_flags::none);  // Send the public key
        client_state = true;  // Mark that a client has connected
    }
    sock.close();  // Clean up socket
    ctx.close();   // Clean up context
}

/**
 * Main function - program entry point
 * Sets up security, creates threads, and manages client connections
 */
int main() {
    // Generate brand new cryptographic keypair on launch for security
    char skey[128] = { 0 };  // Secret key buffer (private key)
    char pkey[128] = { 0 };  // Public key buffer
    if ( zmq_curve_keypair(&pkey[0], &skey[0])) {  // Generate the keypair
        // 1 is failure
        std::cout << "\ncurve keypair gen failed.";
        exit(EXIT_FAILURE);  // Exit program if key generation fails
    }

    // Start worker threads for handling commands and heartbeats
    std::thread command_t(command_thread, skey);      // Thread for handling commands
    std::thread heartbeat_t(heartbeat_thread, skey);  // Thread for monitoring heartbeats

    // Main loop that handles client connections
    while(true) {  // Infinite loop to accept new clients when old ones disconnect
        heartbeat_state = true;  // Reset heartbeat state for new client
        pkey_server(pkey);  // Wait for client to request public key (blocking call)
        std::cout << "Client connected" << std::endl; 

        // Loop while client is connected
        while(true) {  // Monitor client connection
            if (heartbeat_state.load()==false) {  // Check if heartbeat is still active
                std::cout << "Client connectiono dead" << std::endl;
                break;  // Exit inner loop if client disconnected
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Small delay to prevent CPU spinning
        }
    }
}
