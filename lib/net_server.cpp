#include "net_server.hpp"

tcp_connection::tcp_connection(tcp::socket socket, int id, 
					std::function<void (std::size_t, char*, std::size_t)> read_handler,
					std::function<void (std::shared_ptr<tcp_connection>)> disconnect)
  : socket_(std::move(socket)), id_(id), read_handler_(read_handler), disconnect_(disconnect), valid_(true) {
}

void tcp_connection::start() {
	char first_message[] = "server: connected";
	net_message msg(first_message, strlen(first_message));
	send(msg);
	  
	read_header();
}

int tcp_connection::get_id() {
	return id_;
}

bool tcp_connection::valid() {
	return valid_;
}

void tcp_connection::send(net_message msg) {
	// this function takes a net_message by value to force the copy constructor to be called
	// the copy constructor does a deep copy of the underlying data
	// so that the net_message object will survive until the async_write has been completed
	bool write_in_progress = !write_messages_.empty();
	write_messages_.push_back(msg);
	if (!write_in_progress) {
		do_write();
	}
}

void tcp_connection::do_write() {
	// This function starts an async_write call on the first message in the queue.
	// Once the write finishes, it will call the function again
	// until the message queue is empty.
	boost::asio::async_write(socket_, boost::asio::buffer(write_messages_.front().get_data(), 
	    write_messages_.front().get_body_length() + net_message::header_length),
	  [this] (boost::system::error_code ec, std::size_t /*length*/) {
		  if (!ec) {
			  write_messages_.pop_front();
			  if (!write_messages_.empty()) {
				  do_write();
			  }
		  } else {
			  std::cerr << "error with writing to client " << id_ << " with error code: " << ec << std::endl;
		  }
	  });
}

void tcp_connection::read_header() {
	// Every message that gets sent by the net_server / net_client library
	// will start with a header (of net_message::header_length bytes).
	// This header represents how many bytes the body of the message is.
	// So we begin by reading only the header bytes in order to determine
	// how many bytes we need to ready for the body.
	auto self(shared_from_this());
	boost::asio::async_read(socket_, boost::asio::buffer(read_message_.get_data(), net_message::header_length),
	  boost::bind(&tcp_connection::handle_read_header, self, boost::asio::placeholders::error,
	  boost::asio::placeholders::bytes_transferred));
}

void tcp_connection::handle_read_header(const boost::system::error_code e, std::size_t bytes_transferred) {
	// If a client has disconnected, one of these two errors will be returned
	// Otherwise, we decode the header in order to figure out the length of the body
	// Then we call read_body()
	if (e == boost::asio::error::eof || e == boost::asio::error::connection_reset) {
		disconnect_(shared_from_this());
	} else {
		read_message_.decode_header();
		read_body();
	}
}

void tcp_connection::read_body() {
	// After decoding the header to figure out the length of the body,
	// we can now read the body of the message.
	auto self(shared_from_this());
	boost::asio::async_read(socket_, boost::asio::buffer(read_message_.get_data() + net_message::header_length, read_message_.get_body_length()),
	  boost::bind(&tcp_connection::handle_read_body, self, boost::asio::placeholders::error,
	  boost::asio::placeholders::bytes_transferred));
}

void tcp_connection::handle_read_body(const boost::system::error_code e, std::size_t bytes_transferred) {
	// The body has been read into the read_message_ variable (of type net_message)
	// We will now extract the message into a specific char array so that we can
	// send it to the application server using the read_handler callback they provided
	// in the constructor.
	// Afterwards, we will go back to the read_header() function to start the read loop over again.
	char body[read_message_.get_body_length() + 1];
	memcpy(body, read_message_.get_body(), read_message_.get_body_length());
	body[read_message_.get_body_length()] = '\0';
	// call the read_handler from the net_server object
	read_handler_(id_, body, read_message_.get_body_length());
	read_header();
}

net_server::net_server(boost::asio::io_context& io_context, std::size_t port,
			   std::function<void (std::size_t, bool)> accept_handler,
	           std::function<void (std::size_t, char*, std::size_t)> read_handler)
  : io_context_(io_context), acceptor_(io_context, tcp::endpoint(tcp::v4(), 1234)),
    accept_handler_(accept_handler), read_handler_(read_handler), next_id_(0)  {
		start_accept();
}

void net_server::client_disconnect(std::shared_ptr<tcp_connection> connection) {
	// Whenever a client connects and a tcp_connection object is created
	// to represent that client, this disconnect function will be passed to it.
	// That way, when the tcp_connection object receives a notification that it 
	// the client has disconnected, it can remove itself from the connections_ list.
	std::size_t id = connection->get_id();
	{
		std::scoped_lock lock(connections_mutex_);
		connections_.remove(connection);
	}
	accept_handler_(id, false);
}

void net_server::start_accept() {
	// Anytime a client tries to connect to the server, the lambda function below
	// will be called. 
	// We give each connection a unique id that is continuously increasing.
	// We also pass the connection object the read_handler that was passed in
	// by the application server and the client_disconnect function from above.
	// Each connection is added to the connections_ list which is a list of
	// shared_ptr so that when the list gets reallocated, the connection objects themselves
	// don't need to be moved in memory.
	// Once all of the connection setup is finished, we let the application server
	// know that a new user has connected by calling the accept_handler function that 
	// they provided us.
	acceptor_.async_accept(
	  [this](boost::system::error_code ec, tcp::socket socket) {
		if (!ec) {
			std::unique_lock lock(connections_mutex_);
			std::size_t id = next_id_++;
			connections_.push_back(std::make_shared<tcp_connection>(std::move(socket), id, read_handler_, 
									std::bind(&net_server::client_disconnect, this, std::placeholders::_1)));
			auto connection = connections_.back();
			lock.unlock();

			connection->start();
			accept_handler_(id, true);
		}
		start_accept();
	  });
}

void net_server::send_to(std::size_t id, const char* body, std::size_t length) {
	// Function used to send a message to a specific client
	// We could optimize  the search in the future by leveraging the fact that the id's
	// are constantly increasing so we know that the list of connections will be in
	// sorted order according to the id.
	std::shared_ptr<tcp_connection> connection = find_connection(id);
	if (!connection) {
		std::cerr << "Attempting to send a message to client " << id << ", but client not found." << std::endl;
		return;
	}
	net_message msg(body, length);
	connection->send(msg);
	return;
}

void net_server::send_to_all(const char* body, std::size_t length) {
	// Function called to send a message to every client.
	net_message msg(body, length);
	for (auto& connection : connections_) {
		// send function takes a net_message by value so the copy constructor gets called
		// this constructor makes a deep copy of the underlying data
		// so each connection object will have its own copy of the data
		// this way, the data won't go out of scope and destroy itself
		// before the async_write has finished
		// it could actually be a better idea to instead have a queue of net_messages
		// within the net_server object and have the size of the queue be quite large
		// then anytime the queue exceeds its max size, pop items until it fits again
		// this way, we won't need copies of each net_message object, and as long as the
		// server isn't flooded with too many messages at a time, each net_message object
		// should survive in the queue long enough for every async_write to finish
		if (!connection->valid()) continue;
		connection->send(msg);
	}
}

void net_server::send_to_all_except(std::size_t id, const char* body, std::size_t length) {
	// Function called to send a message to every client except 1.
	net_message msg(body, length);
	for (auto& connection : connections_) {
		if (connection->get_id() == id) continue;
		if (!connection->valid()) continue;
		connection->send(msg);
	}
}

std::shared_ptr<tcp_connection> net_server::find_connection(std::size_t id) {
	// connections_ list is always guaranteed to be sorted according to id
	auto iterator = std::lower_bound(connections_.begin(), connections_.end(), id,
	  [](const std::shared_ptr<tcp_connection>& c1, const std::size_t& id) {
		return c1->get_id() < id;
	});
	
	if (iterator == connections_.end()) {
		return nullptr;
	}
	
	return *iterator;
}

