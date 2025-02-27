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

std::atomic<bool> heartbeat_state (true);
std::atomic<bool> connection_state (false);
std::atomic<bool> abort_state (false);

bool ends_with_suffix(const std::string& str, const std::string& suffix) {
    if (str.length() >= 4) {
        return str.substr(str.length() - 4) == suffix;
    }
    return false;
}

void command_thread(std::string server_pkey, std::string client_pkey, std::string client_skey) {
    const size_t chunk_size = 65536;
    zmq::context_t ctx;
    zmq::socket_t command_sock (ctx, zmq::socket_type::req);
    //command_sock.set(zmq::sockopt::sndtimeo, 10000);
    //command_sock.set(zmq::sockopt::rcvtimeo, 10000);
    command_sock.set(zmq::sockopt::curve_serverkey, server_pkey);
    command_sock.set(zmq::sockopt::curve_publickey, client_pkey);
    command_sock.set(zmq::sockopt::curve_secretkey, client_skey);
    command_sock.set(zmq::sockopt::linger, 1); // Close immediately on disconnect
    command_sock.connect("tcp://localhost:5556");
    
    std::string input;
    while (true) {
        if(abort_state.load()==true) {
            break;
        }
        std::getline(std::cin, input);
        std::stringstream ss(input);
        std::string arg;
        std::vector<std::string> args;
        while (ss >> arg) {
            args.push_back(arg);
        }

        // Sanity checks on input before sending to server
        int num_args = args.size();
        std::string command;
        if (num_args > 0) {
            command = args[0];
            if ( command == "send") {
                if(num_args == 1) {
                    std::cout << "Please provide a .bsz file" << std::endl;
                    continue;
                }
                if(!ends_with_suffix(args[1],"bsz")) {
                    std::cout << "Only .bsz files can be sent" << std::endl;
                    continue;
                }
                std::cout << "Sending:" << args[1] << std::endl;
            } else if (command == "get") {
                if(num_args == 1) {
                    std::cout << "Please provide image filename" << std::endl;
                    continue;
                }
            } else if (command == "exit") {
                std::cout << "now" << std::endl;
                break;
            } else if (command == "render") {
                std::string compoundArg;
                if(num_args > 1) {
                    for (size_t i = 1; i < args.size(); ++i) {
                        compoundArg += args[i];
                        if (i < args.size() - 1) {
                            compoundArg += " "; // Add spaces between arguments
                        }
                    }
                    std::cout << compoundArg << std::endl;
                }
            } else if (command == "hello") {
                ;
            } else {
                std::cout << "unknown" << std::endl;
                continue;
            }        
        }

        // Sanity check input complete
        // Push to server over encrypted socket
        zmq::message_t server_response;
        zmq::message_t msg_command(command);
        //>>>ZOUT
        command_sock.send(msg_command, zmq::send_flags::none); //SEND
        std::cout << "Sent: " << input.data() << std::endl;

        //ZIN<<<
        command_sock.recv(server_response, zmq::recv_flags::none); //RECV
        std::string response_str(static_cast<char*>(server_response.data()), server_response.size()-1);

        if(response_str=="RDY") { // Server acknowledges readiness for multi message commands
            std::cout << "Server Readiness: " << response_str << std::endl;
            if(input == "exit") {
                break;
            // RENDER
            } else if(command == "render") {
                //>>>ZOUT
                command_sock.send(zmq::message_t("render"), zmq::send_flags::none);
                //ZIN<<<
                command_sock.recv(server_response, zmq::recv_flags::none);
            } else if(command == "stat") {
                //>>>ZOUT
                command_sock.send(zmq::message_t("stat"), zmq::send_flags::none);
                //ZIN<<<
                command_sock.recv(server_response, zmq::recv_flags::none);

            // GET
            } else if(command == "get") {
                std::ofstream output_file("orange-juice.png", std::ios::binary); // Open file in binary mode
                std::cout << "getting\n";
                if (!output_file.is_open()) {
                    std::cerr << "Error opening file for writing" << std::endl;
                    std::cout << "ERR" << std::endl;
                    continue; // Don't bother server
                } else {
                    while (true) {
                        //>>>ZOUT
                        command_sock.send(zmq::message_t("GO"), zmq::send_flags::none); 
                        zmq::message_t recv_data;
                        //ZIN<<<
                        command_sock.recv(recv_data, zmq::recv_flags::none); // data transfer

                        // inline messaging with data, breaks to exit loop
                        if (recv_data.size() < 8) {
                            std::string recv_string(static_cast<const char*>(recv_data.data()), recv_data.size()-1);
                            //std::string recv_string = recv_data.to_string();
                            if (recv_string == "EOF") {
                                std::cout << "EOF" << std::endl;
                                break; // End of file 
                            } else if(recv_string == "ERR")  { //LIKELY ERR\0 from client, can't find file
                                std::cout << "ERR client read ACK" << std::endl;
                                break; // Err
                            } else {
                                std::cout << "HUH" << recv_string << std::endl;
                                break;
                            }
                        }
                        // by reaching this point we assume binary data ( even 8 bytes will reach here )
                        std::cout << recv_data.size() << std::endl;
                        output_file.write(static_cast<char*>(recv_data.data()), recv_data.size());
                    }
                    output_file.close();
                }
            // SEND
            } else if(command == "send") {
                std::string read_file = "./orange-juice.bsz";
                std::cout << "sending\n";
                std::ifstream binaryInputFile;
                binaryInputFile.open(read_file, std::ios::binary);// for reading
                if (!binaryInputFile.is_open()) {
                    std::cerr << "Error opening file for read" << std::endl;
                    //>>>ZOUT
                    command_sock.send(zmq::message_t("ERR"), zmq::send_flags::none); 
                    ///ZIN<<<
                    command_sock.recv(server_response, zmq::recv_flags::none);
                } else {
                    std::vector<char> send_buffer(chunk_size);
                    std::streamsize bytes_read_in_chunk;
                    while (true) {
                        binaryInputFile.read(send_buffer.data(), chunk_size); // read the file into the buffer
                        bytes_read_in_chunk = binaryInputFile.gcount(); // Actual bytes read
                        if(bytes_read_in_chunk > 0){
                            zmq::message_t message(send_buffer.data(), bytes_read_in_chunk);
                            //>>>ZOUT 
                            command_sock.send(message, zmq::send_flags::none);
                            //ZIN<<<
                            command_sock.recv(server_response, zmq::recv_flags::none);
                        } else {
                            break;
                        }
                    }
                    //<<<ZOUT
                    command_sock.send(zmq::message_t("EOF"), zmq::send_flags::none);
                    //ZIN>>>
                    command_sock.recv(server_response, zmq::recv_flags::none);
                }
            }
        } else {
            std::cout << "Server response: " << response_str << std::endl;
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
    heartbeat_sock.set(zmq::sockopt::linger, 1); // Close immediately on disconnect
    heartbeat_sock.connect("tcp://localhost:5555");
    int heartbeat_count = 0;
    std::vector<zmq::pollitem_t> items = {};
    while (true) {
        if(abort_state.load()==true) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if(connection_state == true) {
            heartbeat_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);
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
                connection_state = false;
                break;
            }
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
    connection_state = true;
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
            abort_state==true;
            break;
        }
        if (connection_state.load() ==  false) {
            std::cout << "Dead2" << std::endl;
            abort_state==true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    abort_state==true;
    command_t.join();
    heartbeat_t.join();
    return 0;
}

