#include "net_message.hpp"

net_message::net_message()
  : body_length_(0)
{}

net_message::net_message(const char* body, std::size_t length) 
  : body_length_(length)
{
	if (length > max_body_length) {
		std::cerr << "message length exceeds max_body_length and will be trimmed accordingly" << std::endl;
	}
	
	// encode the header with the length of the body
	char header[header_length + 1] = "";
	std::sprintf(header, "%4d", static_cast<int>(body_length_));
	std::memcpy(data_, header, header_length);
	
	// copy the body into the body segment of data_
	std::memcpy(data_ + header_length, body, body_length_);
}

net_message::net_message(const net_message& other) {
	// For copying, we want to make a distinct copy of the data.
	// This is necessary because the async_write calls return immediately
	// but they need the net_message variables to stay valid until the 
	// async_write is actually completed.
	body_length_ = other.body_length_;
	std::memcpy(data_, other.data_, other.body_length_ + header_length);
}

net_message& net_message::operator=(const net_message& other) {
	body_length_ = other.body_length_;
	std::memcpy(data_, other.data_, other.body_length_);
	return *this;
}

const char* net_message::get_data() const {
	return data_;
}

char* net_message::get_data() {
	return data_;
}

const char* net_message::get_body() const {
	return data_ + header_length;
}

char* net_message::get_body() {
	return data_ + header_length;
}

std::size_t net_message::get_body_length() const {
	return body_length_;
}

void net_message::decode_header() {
	char header[header_length + 1];
	memcpy(header, data_, header_length);
	body_length_ = std::atoi(header);
	if (body_length_ > max_body_length) {
		std::cerr << "In decode_header(), body_length_ > max_body_length" << std::endl;
	}
}