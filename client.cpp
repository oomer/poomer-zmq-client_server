#include <iostream>
#include <fstream>
#include <filesystem>
#include <random>
#include <thread>
#include <zmq.hpp>

#include <string>
#include <vector>
#include <chrono>
#include <unistd.h> // For STDIN_FILENO

// dynamic char
#include <vector>       

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
        std::cout << "TRY" << std::endl;
    } catch (const zmq::error_t& e) {
        std::cout << "ERROR" << std::endl;
    }

    std::cout << "RECEVIE" << std::endl;

    zmq::message_t z_in;
    pubkey_sock.recv(z_in);
    std::string pub_key = z_in.to_string();
    pubkey_sock.close();
    ctx.close();
    return pub_key;
}

int main()
{
    try 
    {
        const size_t chunk_size = 32768;
        // Dynamically create keypair, every run is bespoke
        // [TODO] send pubkey to server, mkdir, render to that dir
        char client_skey[128] = { 0 };
        char client_pkey[128] = { 0 };
        if ( zmq_curve_keypair(&client_pkey[0], &client_skey[0])) {
            // 1 is fail
            std::cout << "\ncurve keypair gen failed.";
            exit(EXIT_FAILURE);
        }

        // Get server pubkey, set client keypair
        std::string server_pkey = get_pubkey_from_srv();
        if(server_pkey.empty()) {
            std::cout << "Server is Down" << std::endl;
            return 1;
        }
        
        zmq::context_t ctx(1);

        // Create zmq sockets
        zmq::socket_t heartbeat_sock (ctx, zmq::socket_type::req);
        zmq::socket_t command_sock (ctx, zmq::socket_type::req);

        // Encrypt heartbeat socket 
        heartbeat_sock.set(zmq::sockopt::curve_serverkey, server_pkey);
        heartbeat_sock.set(zmq::sockopt::curve_publickey, client_pkey);
        heartbeat_sock.set(zmq::sockopt::curve_secretkey, client_skey);

        // Encrypt command socket 
        command_sock.set(zmq::sockopt::curve_serverkey, server_pkey);
        command_sock.set(zmq::sockopt::curve_publickey, client_pkey);
        command_sock.set(zmq::sockopt::curve_secretkey, client_skey);
        
        std::cout << "keypair" << std::endl; 

        // Set receive timeout to 1000 milliseconds
        //command_sock.set(zmq::sockopt::sndtimeo, 100000);
        //command_sock.set(zmq::sockopt::rcvtimeo, 1000);
        command_sock.set(zmq::sockopt::linger, 1); // Close immediately on disconnect

        //zmq::context_t ctx(1);
        //zmq::socket_t heartbeat_sock (ctx, zmq::socket_type::req);
        //zmq::socket_t command_sock (ctx, zmq::socket_type::req);
        //sock.set(zmq::sockopt::sndtimeo, 10000);
        //sock.set(zmq::sockopt::rcvtimeo, 10000);
        //command_sock.set(zmq::sockopt::linger, 1); // Close immediately on disconnect
                                           //
        heartbeat_sock.connect("tcp://localhost:5555");
        command_sock.connect("tcp://localhost:5556");

        std::vector<zmq::pollitem_t> items = {
            { heartbeat_sock, 0, ZMQ_POLLOUT, 0 },   // Monitor sender1 for send readiness
            { 0, STDIN_FILENO, ZMQ_POLLIN, 0 }      // Monitor std::cin
        };

        int heartbeat_count = 0;
        while (true) {
            zmq::poll(items, 100); 
            if (items[0].revents & ZMQ_POLLOUT) {
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
                    break;
                }
            }

            if (items[1].revents & ZMQ_POLLIN) {
                // Gather input from console
                std::string input;
                std::getline(std::cin, input);
                // Parse the line

                zmq::message_t msg_command (input);
                command_sock.send(msg_command, zmq::send_flags::none);
                std::cout << "Sent: " << input.data() << std::endl;

                // Wait for response (poll for ZMQ_POLLIN)
                zmq::pollitem_t response_item = { command_sock, 0, ZMQ_POLLIN, 0 };
                zmq::poll(&response_item, 1, 100); // Wait for response with timeout

                /*if (response_item.revents & ZMQ_POLLIN) {
                    zmq::message_t zmq_response;
                    command_sock.recv(zmq_response, zmq::recv_flags::none);
                    std::string response(static_cast<char*>(zmq_response.data()), zmq_response.size());
                    std::cout << "Server Response: " << response << std::endl;
                } else {
                    std::cout << "Server Timeout" << std::endl;
                    break;
                }*/
                zmq::message_t zmq_response;
                command_sock.recv(zmq_response, zmq::recv_flags::none);
                std::string response(static_cast<char*>(zmq_response.data()), zmq_response.size());
                std::cout << "Server Response: " << response << std::endl;

                if(input == "exit") {
                    break;
                } else if(input == "send") {
                    std::string read_file = "./orange-juice.bsz";
                    std::cout << "sending\n";
                    std::ifstream binaryInputFile;
                    binaryInputFile.open(read_file, std::ios::binary);// for reading
                    //std::ifstream binaryInputFile(read_file, std::ios::binary);
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
                            //zmq::message_t message("");
                            //zmq::message_t z_in;
                            //command_sock.send(message, zmq::send_flags::none);
                            //command_sock.recv(z_in); // Wait for acknowledgment from server
                            break;
                        }
                    }
                    // Send an empty message to signal end of file
                    command_sock.send(zmq::message_t(), zmq::send_flags::none);
                    zmq::message_t z_in;
                    command_sock.recv(z_in); // Wait for acknowledgment from server
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        heartbeat_sock.close();
        command_sock.close();
        ctx.close();
        return 0;
    } catch (const zmq::error_t& e) {
        std::cerr << "ZeroMQ error: " << e.what() << std::endl;
        return 1;
    }
}

