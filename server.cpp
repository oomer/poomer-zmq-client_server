#include <iostream>
#include <fstream>
#include <thread>
#include <zmq.hpp>
#include <vector>
#include <chrono>

#include <sstream> // For string streams
#include <atomic>

#include <thread>

std::atomic<bool> heartbeat_state (true);
std::atomic<bool> client_state (false);

void command_thread(std::string server_skey) {

    zmq::context_t ctx;
    zmq::socket_t command_sock(ctx, zmq::socket_type::rep);  
    //command_sock.set(zmq::sockopt::sndtimeo, 10000);
    //command_sock.set(zmq::sockopt::rcvtimeo, 10000);
    command_sock.set(zmq::sockopt::curve_server, true);
    command_sock.set(zmq::sockopt::curve_secretkey, server_skey);
    //command_sock.set(zmq::sockopt::linger, 100); // Close immediately on disconnect
    command_sock.bind("tcp://*:5556");
    zmq::message_t client_response; 

    try {
        std::string write_file = "./oomer.bsz";            
        std::string read_file = "./oomer.png";            
        const size_t chunk_size = 65536;
        std::vector<char> sftp_buffer(chunk_size); // Buffer to hold each chunk
        std::ofstream binaryOutputFile;// for writing
        std::ifstream binaryInputFile;// for reading
        while (true) {
            zmq::message_t msg_command; 
            //ZIN<<<
            command_sock.recv(msg_command, zmq::recv_flags::none);
            std::string client_command = msg_command.to_string();
            std::cout << "Command: " << client_command << std::endl;

            if(client_command == "hello"){
                std::cout << "bye" << std::endl;
                //>>>ZOUT
                command_sock.send(zmq::message_t("bye"), zmq::send_flags::none); 
            } else if (client_command == "exit") {
                std::cout << "exit" << std::endl;
                //>>>ZOUT
                command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none); 
            // RENDER
            } else if (client_command == "render") {
                //engine.scene().read("./oomer.bsz");
                //engine.scene().camera()["resolution"] = Vec2 {200, 200};
                //engine.start();
                std::cout << "start render" << std::endl;
                //>>>ZOUT
                command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none); 

            //GET
            } else if (client_command == "get") { //REP mode
                std::string read_file = "./oomer.png";
                std::cout << "Executing get command\n";
                std::ifstream binaryInputFile;
                binaryInputFile.open(read_file, std::ios::binary);// for reading
                if (!binaryInputFile.is_open()) {
                    std::cerr << "Error opening file for read" << std::endl;
                    //>>>ZOUT
                    command_sock.send(zmq::message_t("ERR"), zmq::send_flags::none); 
                } else {
                    //>>>ZOUT
                    command_sock.send(zmq::message_t("RDY"), zmq::send_flags::none); 
                    std::vector<char> send_buffer(chunk_size);
                    std::streamsize bytes_read_in_chunk;
                    while (true) {
                        zmq::message_t z_in;
                        //ZIN
                        command_sock.recv(z_in);  // Block until zGo, or any message
                        binaryInputFile.read(send_buffer.data(), chunk_size); // read the file into the buffer
                        bytes_read_in_chunk = binaryInputFile.gcount(); // Actual bytes read
                        if(bytes_read_in_chunk > 0){
                            std::cout << bytes_read_in_chunk << std::endl;
                            zmq::message_t message(send_buffer.data(), bytes_read_in_chunk);
                            //ZOUT
                            command_sock.send(message, zmq::send_flags::none); 
                        } else {
                            //ZOUT
                            command_sock.send(zmq::message_t("EOF"), zmq::send_flags::none); 
                            std::cout << "EOF" << std::endl;
                            break; // Exit when 0 bytes read
                        }
                    }
                }

            } else if (client_command == "stat") {
                std::ifstream log_file("logfile.txt");
                if (log_file.is_open()) {
                    std::string log_line;
                    if (std::getline(log_file, log_line)) { // Reads the entire line, including spaces
                        //>>>ZOUT
                        command_sock.send(zmq::message_t(log_line), zmq::send_flags::none); 
                    } else {
                        //>>>ZOUT
                        command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none); 
                    }
                    log_file.close();
                }
            } else if (client_command == "send") {
                std::ofstream output_file("oomer.bsz", std::ios::binary); // Open file in binary mode
                if (!output_file.is_open()) {
                    std::cerr << "Error opening file for writing" << std::endl;
                    //>>>ZOUT
                    command_sock.send(zmq::message_t("ERR"), zmq::send_flags::none); 
                } else { // File handle open and ready
                    //>>>ZOUT
                    command_sock.send(zmq::message_t("RDY"), zmq::send_flags::none); 
                    while (true) {
                        zmq::message_t recv_data;
                        //ZIN<<<
                        command_sock.recv(recv_data, zmq::recv_flags::none);
                        if(recv_data.size() < 8) { // data and signals sent on same socket
                            // Allow for signals up to 8 bytes, EOF, ERR
                            // messages are null terminated requiring -1
                            std::string response_str(static_cast<char*>(recv_data.data()), recv_data.size()-1);
                            if (response_str=="EOF") {
                                //>>>ZOUT
                                command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);
                                break; // End of file
                            } else if(response_str=="ERR")  {
                                std::cout << "ERR on client" << std::endl;
                                //>>>ZOUT
                                command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);
                                break; // End of file
                            }
                        }
                        // File write
                        output_file.write(static_cast<char*>(recv_data.data()), recv_data.size());
                        //>>ZOUT
                        command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none);
                    }
                    output_file.close();
                }
            } else { // A unknown REQ sent, acknowledge because req-rep pattern is blocking
                //>>ZOUT
                command_sock.send(zmq::message_t("ACK"), zmq::send_flags::none); 
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

    } catch (const zmq::error_t& e) {
        // Handle ZMQ-specific exceptions
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

        //Start polling heartbeats once client connects
        if (client_state == true) {
            zmq::pollitem_t response_item = { heartbeat_sock, 0, ZMQ_POLLIN, 0 };
            zmq::poll(&response_item, 1, 25000); // Wait for response with timeout

            if (response_item.revents & ZMQ_POLLIN) {
                zmq::message_t message;
                //ZIN<<<
                heartbeat_sock.recv(message, zmq::recv_flags::none);
                //ZOUT>>>
                heartbeat_sock.send(zmq::message_t("ACK"), zmq::send_flags::dontwait); // No block
            } else { //timeout
                std::cout << "Bella Client Lost" << std::endl;
                heartbeat_state = false;
            }
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
    //ZIN<<<
    sock.recv(z_in);
    if (z_in.to_string().compare("Bellarender123") == 0) {
        zmq::message_t z_out(pub_key);
        //ZOUT>>>
        sock.send(z_out, zmq::send_flags::none);
        client_state = true;
    }
    sock.close();
    ctx.close();
}

// We will use the dl_core main helper here. This gives us a helpful Args instance to use, and
// also hides the confusing details of dealing with main vs. WinMain on windows, and gives us
// utf8-encoded args when the application is unicode.
//
//#include "dl_core/dl_main.inl"
//int DL_main(Args& args)
//{

int main() {
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
    std::thread command_t(command_thread, skey);
    std::thread heartbeat_t(heartbeat_thread, skey);

                                       //
    while(true) { // awaiting new client loop
        heartbeat_state = true;
        pkey_server(pkey); // blocking wait client to get public key on port 5555
        std::cout << "Client connected" << std::endl; 

        while(true) { // inner loop
            if (heartbeat_state.load()==false) {
                std::cout << "Client connectiono dead" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
