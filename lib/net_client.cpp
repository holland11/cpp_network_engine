#include "net_client.hpp"


net_client::net_client(boost::asio::io_context& io_context, std::size_t port,
	           std::function<void (char*, std::size_t)> read_handler) 
  : io_context_(io_context), socket_(io_context), read_handler_(read_handler) {
	connect(port);
}

void net_client::connect(std::size_t port) {
	// The first thing that we do after the constructor has initialized its members
	// is connect to the server.
	// As of now, only local network connections are supported so we will
	// always connect to 127.0.0.1
	// After connecting, we call read_header() to start the read loop.
	socket_.connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"), port));
		
	read_header();
}

void net_client::read_header() {
	// Similar to the server's read loop, we start by reading net_message::header_length bytes
	// from every message so that we can figure out how many bytes the body of message is.
	boost::asio::async_read(socket_,
	  boost::asio::buffer(read_message_.get_data(), net_message::header_length),
	  boost::bind(&net_client::handle_read_header, this, boost::asio::placeholders::error,
	  boost::asio::placeholders::bytes_transferred));
}

void net_client::handle_read_header(const boost::system::error_code e, std::size_t bytes_transferred) {
	// Decode the header and call read_body()
	read_message_.decode_header();
	read_body();
}

void net_client::read_body() {
	// Read the body of the message into the read_message_ member variable (which is of type net_message).
	// We know how many bytes the body is because we decoded the header above.
	boost::asio::async_read(socket_, boost::asio::buffer(read_message_.get_data() + net_message::header_length, read_message_.get_body_length()),
	  boost::bind(&net_client::handle_read_body, this, boost::asio::placeholders::error,
	  boost::asio::placeholders::bytes_transferred));
}

void net_client::handle_read_body(const boost::system::error_code e, std::size_t bytes_transferred) {
	// The body of the message is now located at read_message_.get_body()
	// Let's copy the body of the message into a local char array and forward it
	// to the application client so they can do some processing if they want.
	// Afterwards, we call read_header() to start the read loop over again.
	char body[read_message_.get_body_length()];
	memcpy(body, read_message_.get_body(), read_message_.get_body_length());
	// call the read_handler that was passed in by the application
	read_handler_(body, read_message_.get_body_length());
	read_header();
}

void net_client::send(const char* body, std::size_t length) {
	// For sending messages, we use an outgoing message queue.
	// Anytime the client wants to send a message, this function will be called.
	// If the message queue is not currently empty, do_write() will be called.
	// do_write() will call itself recursively until every message has been sent from
	// the queue.
	// Because of this, we know that if the message queue is not already empty,
	// do_write() must still be in progress so we don't need to call it again.
	bool write_in_progress = !write_messages_.empty();
	write_messages_.emplace_back(body, length);
	if (!write_in_progress) {
		do_write();
	}
}

void net_client::do_write() {
	// The function that repeatedly starts async_write calls until the 
	// message queue is empty.
	boost::asio::async_write(socket_, boost::asio::buffer(write_messages_.front().get_data(), 
	    write_messages_.front().get_body_length() + net_message::header_length),
	  [this] (boost::system::error_code ec, std::size_t /*length*/) {
		  if (!ec) {
			  write_messages_.pop_front();
			  if (!write_messages_.empty()) {
				  do_write();
			  }
		  } else {
			  std::cerr << "error with writing to server with error code: " << ec << std::endl;
		  }
	  });
}

std::size_t net_client::get_max_body_length() {
	// Getter function that lets the application client 
	// find out what the max_body_length of a message is.
	return net_message::max_body_length;
}