#include <iostream>
#include <fstream>
#include <filesystem>
#include <random>
#include <thread>
#include <zmq.hpp>

#include <string>
#include <vector>
#include <chrono>
#include <vector>       

#include <atomic>
#include <condition_variable>
#include <mutex>

// Atomic variable to store the counter value
std::atomic<int> counter(0);
std::atomic<bool> heartbeat_state (true);
// Condition variable to signal the counter thread
std::condition_variable cv;
// Mutex to protect shared data
std::mutex mtx;
// Flag to indicate if the counter should reset
std::atomic<bool> shouldReset(false);

void command_thread(std::string server_pkey, std::string client_pkey, std::string client_skey) {
    const size_t chunk_size = 32768;
    zmq::context_t ctx;
    zmq::socket_t command_sock (ctx, zmq::socket_type::req);
    command_sock.set(zmq::sockopt::sndtimeo, 10000);
    command_sock.set(zmq::sockopt::rcvtimeo, 10000);
    command_sock.set(zmq::sockopt::curve_serverkey, server_pkey);
    command_sock.set(zmq::sockopt::curve_publickey, client_pkey);
    command_sock.set(zmq::sockopt::curve_secretkey, client_skey);
    command_sock.set(zmq::sockopt::linger, 1); // Close immediately on disconnect
    command_sock.connect("tcp://localhost:5556");

    while (true) {
        std::string input;
        std::getline(std::cin, input); 
        zmq::message_t msg_command (input);
        command_sock.send(msg_command, zmq::send_flags::none);
        std::cout << "Sent: " << input.data() << std::endl;

        zmq::message_t zmq_response;
        command_sock.recv(zmq_response, zmq::recv_flags::none);
        std::string response(static_cast<char*>(zmq_response.data()), zmq_response.size()-1);
        std::cout << "Server Response: " << response << response.size() << std::endl;

        if(response=="ACK") { // Check server is ok to move on
            std::cout << "kACKServer Response: " << response << std::endl;

            if(input == "exit") {
                break;
            } else if(input == "send") {
                std::string read_file = "./orange-juice.bsz";
                std::cout << "sending\n";
                std::ifstream binaryInputFile;
                binaryInputFile.open(read_file, std::ios::binary);// for reading
                if (!binaryInputFile.is_open()) {
                    std::cerr << "Error opening file for read" << std::endl;
                    zmq::message_t zmsg1("ERR");
                    command_sock.send(zmsg1, zmq::send_flags::none); 
                    zmq::message_t z_in;
                    command_sock.recv(z_in); // Wait for acknowledgment from server
                    std::cout << z_in << std::endl;
                } else {
           
                    std::vector<char> send_buffer(chunk_size);
                    std::streamsize bytes_read_in_chunk;
                    while (true) {
                        binaryInputFile.read(send_buffer.data(), chunk_size); // read the file into the buffer
                        bytes_read_in_chunk = binaryInputFile.gcount(); // Actual bytes read
                        if(bytes_read_in_chunk > 0){
                            //std::cout << bytes_read_in_chunk << std::endl;
                            zmq::message_t message(send_buffer.data(), bytes_read_in_chunk);
                            zmq::message_t z_in;
                            command_sock.send(message, zmq::send_flags::none);
                            command_sock.recv(z_in); // Wait for acknowledgment from server
                        } else {
                            break;
                        }
                    }
                    // Send an empty message to signal end of file
                    command_sock.send(zmq::message_t(), zmq::send_flags::none);
                    zmq::message_t z_in;
                    command_sock.recv(z_in); // Wait for acknowledgment from server
                }
            }
        }
    }
    command_sock.close();
    ctx.close();
}

void heartbeat_thread(std::string server_pkey, std::string client_pkey, std::string client_skey) {
    zmq::context_t ctx;
    zmq::socket_t heartbeat_sock (ctx, zmq::socket_type::req);
    heartbeat_sock.set(zmq::sockopt::curve_serverkey, server_pkey);
    heartbeat_sock.set(zmq::sockopt::curve_publickey, client_pkey);
    heartbeat_sock.set(zmq::sockopt::curve_secretkey, client_skey);
    heartbeat_sock.connect("tcp://localhost:5555");
    int heartbeat_count = 0;
    std::vector<zmq::pollitem_t> items = {};
    while (true) {
        // Increment the counter every 10 milliseconds
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        //std::cout << "beat" << std::endl;
        std::string msg_string = "BEAT" + std::to_string(heartbeat_count++);
        zmq::message_t msg_heartbeat (msg_string);
        heartbeat_sock.send(msg_heartbeat, zmq::send_flags::none);


        // Wait for response (poll for ZMQ_POLLIN)
        zmq::pollitem_t response_item = { heartbeat_sock, 0, ZMQ_POLLIN, 0 };
        zmq::poll(&response_item, 1, 100); // Wait for response with timeout
        if (response_item.revents & ZMQ_POLLIN) {
            zmq::message_t msg_response;
            heartbeat_sock.recv(msg_response, zmq::recv_flags::none);
            //std::cout << "Heartbeat Response: " << std::endl;
        } else {
            std::cout << "Bella Server is unavailable" << std::endl;
            heartbeat_state = false;
        }
    }
    heartbeat_sock.close();
    ctx.close();
}


std::string get_pubkey_from_srv() {
    // No authentication is used, server will give out pubkey to anybody
    // Could use a unique message but since socket is unencrypted this provides
    // no protection. In main loop we establish an encrypted connection with the server
    // now that we have the pubkey and in combo with the client_secret_key we can
    // be secure. 0MQ uses PFS perfect forward security, because this initial
    // back and forth is extended with behind the scenes new keypairs taken care of by
    // 0MQ after we establish our intitial encrypted socket
    zmq::context_t ctx;
    zmq::socket_t pubkey_sock(ctx, zmq::socket_type::req);

    pubkey_sock.set(zmq::sockopt::sndtimeo, 10000);
    pubkey_sock.set(zmq::sockopt::rcvtimeo, 10000);
    pubkey_sock.set(zmq::sockopt::linger, 0); // Close immediately on disconnect

    pubkey_sock.connect("tcp://127.0.0.1:9555");
    zmq::message_t z_out(std::string("Bellarender123"));

    try {
        zmq::send_result_t send_result = pubkey_sock.send(z_out, zmq::send_flags::none);
    } catch (const zmq::error_t& e) {
        std::cout << "ERROR" << std::endl;
    }

    std::cout << "bellazmq connecting to server..." << std::endl;
    zmq::message_t z_in;
    pubkey_sock.recv(z_in);
    std::string pub_key = z_in.to_string();
    pubkey_sock.close();
    ctx.close();
    std::cout << "connection successful" << std::endl;
    return pub_key;
}

int main()
{
    const size_t chunk_size = 32768;
    // Dynamically create keypair, every run is bespoke
    // [TODO] send pubkey to server, mkdir, render to that dir
    char client_skey[41] = { 0 };
    char client_pkey[41] = { 0 };
    if ( zmq_curve_keypair(&client_pkey[0], &client_skey[0])) {
        // 1 is fail
        std::cout << "\ncurve keypair gen failed.";
        exit(EXIT_FAILURE);
    }

    // Get server pubkey, set client keypair
    std::string server_pkey = get_pubkey_from_srv();
    /*if(server_pkey.empty()) {
        std::cout << "Server is Down" << std::endl;
        heartbeat_state = false;
    }*/

    std::string client_pkey_str(client_pkey);
    std::string client_skey_str(client_skey);

    // Multithreaded
    std::thread command_t(command_thread, server_pkey, client_pkey_str, client_skey_str);
    std::thread heartbeat_t(heartbeat_thread, server_pkey, client_pkey_str, client_skey_str);

    while (true) {
        if (!heartbeat_state.load()) {
            std::cout << "Dead" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    command_t.join();
    heartbeat_t.join();
    return 0;
}

