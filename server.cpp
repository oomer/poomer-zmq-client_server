#include <iostream>
#include <fstream>
#include <thread>
#include <zmq.hpp>
#include <unistd.h>
#include <vector>
#include <chrono>

// Blocking zmq rep socket to pass server_public_key
void pkey_server(const std::string& pub_key) {
    zmq::context_t ctx;
    zmq::socket_t sock(ctx, zmq::socket_type::rep);
    sock.bind("tcp://*:9555"); //[TODO] args to set port

    zmq::message_t z_in;
    std::cout << "Entered: Public Key Serving Mode" << std::endl; 
    sock.recv(z_in);
    if (z_in.to_string().compare("Bellarender123") == 0) {
        zmq::message_t z_out(pub_key);
        sock.send(z_out, zmq::send_flags::none);
    }
    sock.close();
    ctx.close();
}

int main()
{
    zmq::context_t ctx(1);

    // Create zmq rep sockets
    zmq::socket_t heartbeat_sock(ctx, zmq::socket_type::rep);
    zmq::socket_t command_sock(ctx, zmq::socket_type::rep);  

    // Generate brand new keypair on launch
    // [TODO] Add client side public key fingerprinting for added security
    char skey[128] = { 0 };
    char pkey[128] = { 0 };
    if ( zmq_curve_keypair(&pkey[0], &skey[0])) {
        // 1 is fail
        std::cout << "\ncurve keypair gen failed.";
        exit(EXIT_FAILURE);
    }

    heartbeat_sock.set(zmq::sockopt::curve_server, true);
    heartbeat_sock.set(zmq::sockopt::curve_secretkey, skey);

    command_sock.set(zmq::sockopt::curve_server, true);
    command_sock.set(zmq::sockopt::curve_secretkey, skey);

    //heartbeat_sock.set(zmq::sockopt::rcvtimeo, 10000);
    //heartbeat_sock.set(zmq::sockopt::sndtimeo, 10000);
    command_sock.set(zmq::sockopt::linger, 1); // Close immediately on disconnect

    // Binding to transport
    heartbeat_sock.bind("tcp://*:5555");
    command_sock.bind("tcp://*:5556");

    // Create poll items
    std::vector<zmq::pollitem_t> items = {
        { heartbeat_sock, 0, ZMQ_POLLIN, 0 },
        { command_sock, 0, ZMQ_POLLIN, 0 }
    };

                                       //
    while(true) { // awaiting client loop
        pkey_server(pkey); // blocking wait client to get public key on port 5555
        std::cout << "Client connected" << std::endl; 
        int heartbeat_miss = 0;
        while (true) { //loop forever accepting encrypted messages, limit to one client
            zmq::poll(items, 100);    
            //std::cout << "heart:" << heartbeat_miss << std::endl;

             // Check if heartbeat_socket has data
            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t message;
                heartbeat_sock.recv(message, zmq::recv_flags::none);
                //std::cout << "heart:" << heartbeat_miss << std::endl;

                std::string response = "Heartbeat OK";
                zmq::message_t zmq_response (response);
                heartbeat_sock.send(zmq_response, zmq::send_flags::dontwait); // No block
                heartbeat_miss = 0; // Reset heartbeat misses
            } else { //No heartbeat detected during poll
                heartbeat_miss++;
                if (heartbeat_miss>25) { //This many misses means client is AWOL
                    break; //Exit inner loop to outer loop handling pubkey serving
                }
            }    
            // Check if command_socket has data
            if (items[1].revents & ZMQ_POLLIN) {
                zmq::message_t msg_command;
                command_sock.recv(msg_command, zmq::recv_flags::none);
                std::string client_command = msg_command.to_string();
                //std::string received_message (static_cast<char*>(msg_command.data()), msg_command.size());
                std::cout << "Command: " << client_command << std::endl;

                // Send a response
                //zmq::message_t zmq_response("ACK");
                //command_sock.send(zmq_response, zmq::send_flags::none);
                
                //std::string client_command = msg_command.to_string();

                // 2. Check if the string is empty
                //if (client_command.empty()) {
                //    std::cerr << "Invalid message received: "  << std::endl;
                //    break; // exit loop to await new client
                //}
                
                if(client_command == "hello"){
                    zmq::message_t zmsg1("bye");
                    command_sock.send(zmsg1, zmq::send_flags::none); 
                } else if (client_command == "exit") {
                    zmq::message_t zmsg1("exit");
                    command_sock.send(zmsg1, zmq::send_flags::none); 
                    break;
                } else if (client_command == "send") {
                    std::ofstream output_file("received_file.bsz", std::ios::binary); // Open file in binary mode
                    if (!output_file.is_open()) {
                        std::cerr << "Error opening file for writing" << std::endl;
                        return 1;
                    }
                    zmq::message_t zmsg1("ACK1");
                    command_sock.send(zmsg1, zmq::send_flags::none); 
                    while (true) {
                        zmq::message_t recv_data;
                        command_sock.recv(recv_data, zmq::recv_flags::none);
                        if (recv_data.size() == 0) {
                            zmq::message_t reply("ACK2");
                            command_sock.send(reply, zmq::send_flags::none);
                            break; // End of file
                        }
                        //std::cout << recv_data.size() << std::endl;
                        output_file.write(static_cast<char*>(recv_data.data()), recv_data.size());
                        zmq::message_t reply("ACK2");
                        command_sock.send(reply, zmq::send_flags::none);
                    }
                    output_file.close();
                } else {
                    zmq::message_t zmsg1("ACK");
                    command_sock.send(zmsg1, zmq::send_flags::none); 
                }
            }
            // Simulate doing other work (optional)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
