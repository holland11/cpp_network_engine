#ifndef _NET_CLIENT_HPP_
#define _NET_CLIENT_HPP_

#include <deque>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "net_message.hpp"

/*

The net_client class is a class that will handle the network connections and messages for your client application.

To use the net_client class in your application, your main function should look something like:

int main() {
	try {
		boost::asio::io_context io_context;
		std::size_t port_num = 1234;
		application_client client(io_context, port_num);
		
		std::thread io_thread([&io_context](){ io_context.run(); });
		client.start();
		
		io_thread.join();
		
	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
	
	return 0;
}

Where application_client is a class that looks something like:

class application_client {
public:
	application_client(boost::asio::io_context& io_context, std::size_t port) 
	  : io_context_(io_context), client_(io_context, port,
	      std::bind(&application_client::handle_read, this, std::placeholders::_1, std::placeholders::_2)) {
		max_body_length_ = client_.get_max_body_length();
	}
	
	void start() {
		write_loop();
	}
	
private:
	void write_loop() { 
		char message[max_body_length_];
		
		// read user input into the message array
		// then send the message to the server
		
		client_.send(message, strlen(message));
		
		write_loop();
	}
	
	void handle_read(char* body, std::size_t length) {
		// This function will be called whenever the client
		// receives a message
		// body will be a pointer to the first char in the message
		// length will be the length of the message
	}
	
	net_client client_;
	std::size_t max_body_length_;
};

*/

class net_client {
public:
	net_client(boost::asio::io_context& io_context, std::size_t port,
	           std::function<void (char*, std::size_t)> read_handler);
			   
	void send(const char* body, std::size_t length);
			   
	std::size_t get_max_body_length();
private:
	void connect(std::size_t port);
	void read_header();
	void handle_read_header(const boost::system::error_code e, std::size_t bytes_transferred);
	void read_body();
	void handle_read_body(const boost::system::error_code e, std::size_t bytes_transferred);
	void do_write();

	boost::asio::io_context& io_context_;
	boost::asio::ip::tcp::socket socket_;
	
	net_message read_message_;
	std::deque<net_message> write_messages_;
	
	std::function<void (char*, std::size_t)> read_handler_;
};

#endif