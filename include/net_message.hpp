#ifndef _NET_MESSAGE_HPP_
#define _NET_MESSAGE_HPP_

#include <cstddef>
#include <iostream>
#include <cstring>

/*

the net_message class is a class that represents a message (packet)
that is sent between a client and a server (in either direction)

a message has a variable length where the first header_length bytes signify
how many bytes in length the rest of the message is

*/



class net_message {
public:
	enum { header_length = 4 };
	enum { max_body_length = 512 };
	
	net_message(); // default constructor that sets body_length_ to 0
	net_message(const char* body, std::size_t length); // constructor that takes in the body of the message
	
	net_message(const net_message& other); // copy constructor explicit bcz we want to deep copy the data
	net_message& operator=(const net_message& other); // same as copy constructor but assignment
	
	// no memory management so don't need a special destructor
	
	const char* get_data() const;
	char* get_data();
	const char* get_body() const;
	char* get_body();
	std::size_t get_body_length() const;
	void decode_header();
	
private:
	char data_[header_length + max_body_length];
	std::size_t body_length_;
};

#endif