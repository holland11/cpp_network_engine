#ifndef _NET_SERVER_HPP_
#define _NET_SERVER_HPP_

#include <deque>
#include <list>
#include <algorithm>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "net_message.hpp"

/*
The net_server class is a class that will handle the network connections and messages for your server application.

To use the net_server class in your application, your main function should look something like:

int main() {
	try {
		boost::asio::io_context io_context;
		std::size_t port_num = 1234;
		application_server serv(io_context, port_num);
		io_context.run();
	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
	
	return 0;
}

Where application_server is a class that looks something like:

class application_server {
public:
	application_server(boost::asio::io_context& io_context, std::size_t port)
	  : server_(io_context, port, std::bind(&application_server::handle_accept, this, std::placeholders::_1, std::placeholders::_2),
	    std::bind(&application_server::handle_read, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
	{}
	
private:
	void handle_accept(std::size_t client_id, bool connect) {
		// function called when a client connects or disconnects from the server
		// client_id is the id of the client that connected/disconnected
		// connect is true if it is a connection and false if it is a disconnection
	}
	
	void handle_read(std::size_t sender, char* body, std::size_t length) {
		// function called when a client sends a message to the server
		// sender is the id of the client
		// body is a pointer to the first character in the message
		// length is how long the message is
	}
	
	net_server server_; // the net_server object that handles the networking for the application
};


The net_server class itself will maintain a list of connections.
Each message that gets sent by the server or client will be encoded as a net_message object
and then it will be decoded by the receiver.

*/


using boost::asio::ip::tcp;

class net_server;

class application_server {
	// a base class that applications should inherit from
	// in order to use the net_server class as expected
public:
	application_server(std::size_t port) 
	  : server_ptr_(std::make_shared<net_server>(io_context_, port, 
		  std::bind(&application_server::accept_handler, this, std::placeholders::_1, std::placeholders::_2),
	      std::bind(&application_server::read_handler, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))) {
	}
	void start() {
		// if you are going to overwrite this function, 
		// you should still call application_server::start() from inside
		io_context_.run();
	}
	void stop() {
		io_context_.stop();
	}
private:
	virtual void accept_handler(std::size_t client_id, bool connect) {
		// virtual so that when we pass this function to the net_server constructor,
		// it passes the derived version rather than this one
		std::cout << "You need to implement your own version of accept_handler." << std::endl;
	}
	
	virtual void read_handler(std::size_t sender, char* body, std::size_t length) {
		// virtual so that when we pass this function to the net_server constructor,
		// it passes the derived version rather than this one
		std::cout << "You need to implement your own version of read_handler." << std::endl;
	}
	
protected:
	boost::asio::io_context io_context_;
	std::shared_ptr<net_server> server_ptr_;
};

class tcp_connection
  : public std::enable_shared_from_this<tcp_connection> {
	// a class representing a client that has connected to the server
	// as soon as a client connects to the server, a tcp_connection object
	// will be created in the net_server class
	// this object will handle the reading/writing of messages between
	// the server and this client
	// when the client disconnects, this object will be deleted
public:
	tcp_connection(tcp::socket socket_, int id, 
					std::function<void (std::size_t, char*, std::size_t)> read_handler,
					std::function<void (std::shared_ptr<tcp_connection>)> disconnect);
	
	void start();
	void send(net_message msg);
	int get_id();
	bool valid();
	
private:
	void read_header();
	void handle_read_header(const boost::system::error_code e, std::size_t bytes_transferred);
	void handle_read_body(const boost::system::error_code e, std::size_t bytes_transferred);
	void read_body();
	void do_write();
	
	tcp::socket socket_;
	bool valid_;
	std::function<void (std::size_t, char*, std::size_t)> read_handler_;
	std::function<void (std::shared_ptr<tcp_connection>)> disconnect_;
	net_message read_message_;
	std::deque<net_message> write_messages_;
	int id_;
};

class net_server {
public:
	net_server(boost::asio::io_context& io_context, std::size_t port, 
			   std::function<void (std::size_t, bool)> accept_handler,
	           std::function<void (std::size_t, char*, std::size_t)> read_handler);
	
	void send_to(std::size_t id, const char* body, std::size_t length);
	void send_to_all(const char* body, std::size_t length);
	void send_to_all_except(std::size_t id, const char* body, std::size_t length);
		
private:
	void client_disconnect(std::shared_ptr<tcp_connection> connection);
	void start_accept();
	std::shared_ptr<tcp_connection> find_connection(std::size_t id);

	boost::asio::io_context& io_context_;
	tcp::acceptor acceptor_;
	
	std::size_t next_id_;
	std::list<std::shared_ptr<tcp_connection>> connections_;
	std::mutex connections_mutex_;
	
	std::function<void (std::size_t, bool)> accept_handler_; // the bool is true=connection false=disconnect
	std::function<void (std::size_t, char*, std::size_t)> read_handler_;
};

#endif

/*
Notes:

(Not implemented yet)
Because client id's are contantly increasing, it's gauranteed
that the list of connections will be in sorted order. We can leverage
this guarantee to make all of our searches faster instead of iterating
through the entire array from start to finish looking for an id match.
This could also be implemented in the application code (chat_server and connect4_server).

(Not implemented yet)
Could have a base (virtual) class defined in this file called 
net_server_application that has handle_read(...) and handle_accept(...)
functions. Then any applications that want to use the net_server library
could inherit from that base class. 

*/