#include <iostream>
#include <fstream>
#include <thread>
#include <zmq.hpp>
#include <vector>
#include <chrono>

#include <atomic>
#include <condition_variable>
#include <mutex>


// Atomic variable to store the counter value
//std::atomic<int> counter(0);
std::atomic<bool> heartbeat_state (true);
// Condition variable to signal the counter thread
//std::condition_variable cv;
// Mutex to protect shared data
//std::mutex mtx;
// Flag to indicate if the counter should reset
std::atomic<bool> shouldReset(false);


void command_thread(std::string server_skey) {
    zmq::context_t ctx;

    // Create zmq rep sockets
    //zmq::socket_t heartbeat_sock(ctx, zmq::socket_type::rep);
    zmq::socket_t command_sock(ctx, zmq::socket_type::rep);  
    command_sock.set(zmq::sockopt::curve_server, true);
    command_sock.set(zmq::sockopt::curve_secretkey, server_skey);
    command_sock.set(zmq::sockopt::linger, 1); // Close immediately on disconnect
    command_sock.bind("tcp://*:5556");
    while (true) {
        zmq::message_t msg_command;
        command_sock.recv(msg_command, zmq::recv_flags::none);
        std::string client_command = msg_command.to_string();
        std::cout << "Command: " << client_command << std::endl;

        if(client_command == "hello"){
            std::cout << "bye" << std::endl;
            zmq::message_t zmsg1("bye");
            command_sock.send(zmsg1, zmq::send_flags::none); 
        } else if (client_command == "exit") {
            std::cout << "ACK" << std::endl;
            zmq::message_t zmsg1("ACK");
            command_sock.send(zmsg1, zmq::send_flags::none); 
            //heartbeat_state = false;
        } else if (client_command == "send") {
            std::ofstream output_file("received_file.bsz", std::ios::binary); // Open file in binary mode
            if (!output_file.is_open()) {
                std::cerr << "Error opening file for writing" << std::endl;
                std::cout << "ERR" << std::endl;
                zmq::message_t zmsg1("ERR");
                command_sock.send(zmsg1, zmq::send_flags::none); 
            } else {
                std::cout << "file good ACK" << std::endl;
                zmq::message_t zmsg1("ACK");
                command_sock.send(zmsg1, zmq::send_flags::none); 
                while (true) {
                    zmq::message_t recv_data;
                    command_sock.recv(recv_data, zmq::recv_flags::none);
                    if (recv_data.size() == 0) {
                        //std::cout << "chunk ACK" << std::endl;
                        zmq::message_t reply("ACK");
                        command_sock.send(reply, zmq::send_flags::none);
                        break; // End of file
                    } else if(recv_data.size() == 4)  { //LIKELY ERR\0 from client, can't find file
                        // [TODO] , parse lines only send valid commands
                        // Right now I allow any text to get through

                        std::cout << "ERR client read ACK" << std::endl;
                        zmq::message_t reply("ACK");
                        command_sock.send(reply, zmq::send_flags::none);
                        break; // End of file
                    }
                    //std::cout << recv_data.size() << std::endl;
                    output_file.write(static_cast<char*>(recv_data.data()), recv_data.size());
                    zmq::message_t reply("ACK");
                    command_sock.send(reply, zmq::send_flags::none);
                }
                output_file.close();
            }
        } else {
            zmq::message_t zmsg1("foo");
            command_sock.send(zmsg1, zmq::send_flags::none); 
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    command_sock.close();
    ctx.close();
}

void heartbeat_thread(std::string server_skey) {
    heartbeat_state = true;
    std::cout << "new heartbeat_thread" << std::endl;
    zmq::context_t ctx;
    zmq::socket_t heartbeat_sock (ctx, zmq::socket_type::rep);
    heartbeat_sock.set(zmq::sockopt::curve_server, true);
    heartbeat_sock.set(zmq::sockopt::curve_secretkey, server_skey);
    heartbeat_sock.bind("tcp://*:5555");
    while(true) {
        zmq::pollitem_t response_item = { heartbeat_sock, 0, ZMQ_POLLIN, 0 };
        zmq::poll(&response_item, 1, 10000); // Wait for response with timeout

        if (response_item.revents & ZMQ_POLLIN) {
            zmq::message_t message;
            heartbeat_sock.recv(message, zmq::recv_flags::none);
            //std::cout << "heart:" << heartbeat_state.load() << std::endl;
        
            std::string response = "Heartbeat OK";
            zmq::message_t zmq_response (response);
            heartbeat_sock.send(zmq_response, zmq::send_flags::dontwait); // No block
        
        } else {
            std::cout << "Bella Client Lost" << std::endl;
            heartbeat_state = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    heartbeat_sock.close();
    ctx.close();
}

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
    // Generate brand new keypair on launch
    // [TODO] Add client side public key fingerprinting for added security
    char skey[128] = { 0 };
    char pkey[128] = { 0 };
    if ( zmq_curve_keypair(&pkey[0], &skey[0])) {
        // 1 is fail
        std::cout << "\ncurve keypair gen failed.";
        exit(EXIT_FAILURE);
    }

    // Multi threading
    //std::thread command_t(command_thread, skey);
    //std::thread heartbeat_t(heartbeat_thread, skey);
    //std::thread command_t(command_thread, skey);
    //std::thread heartbeat_t(heartbeat_thread, skey);
    std::thread command_t(command_thread, skey);
    std::thread heartbeat_t(heartbeat_thread, skey);

                                       //
    while(true) { // awaiting new client loop
        heartbeat_state = true;
        pkey_server(pkey); // blocking wait client to get public key on port 5555
        heartbeat_state = true;
        std::cout << "Client connected" << std::endl; 

        while(true) { // inner loop
            //std::cout << "inner loop" << std::endl; 
            if (heartbeat_state.load()==false) {
                std::cout << "client dead" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
