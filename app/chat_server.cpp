#include "net_server.hpp"
#include "chat_constants.hpp"

#include <iostream>
#include <sstream>
#include <ncurses.h>

/*

A server-side application for a chatroom. 
This application uses the net_server class to handle its networking.
The application just needs to provide the net_server object with two functions:
One function that gets called whenever a client connects or disconnects (accept_handler),
and one function that gets called whenever a message is received (read_handler).
The application can send messages to clients using the net_server::send_to() function
or any of its variants (send_to_all, send_to_all_except).

Even though the net_server object will maintain a list of active connections,
this chat_server class will also maintain its own list of active clients.

Each client has a unique id that will be the same within the net_server's connection list as well.
Each client also has a unique name that the net_server object doesn't concern itself with.

Clients have a small list of commands that they can send to the server.
Each command starts with a '#' so the server can quickly distinguish beteen normal messages
and commands.

Anytime a client sends a normal message, the server will add the client's name to the message
then send the message back out to every client.

The ncurses library is used for the chatroom, however the server doesn't actually do much with it.
It is used more extensively by the client.

*/

class client {
public:
	client()
	  : valid_(false)
	{}
	client(int id)
	  : id_(id), valid_(true)
	{
		sprintf(name, "Client%u", id_);
	}
	
	const char * get_name() const {
		return name;
	}
	
	void set_name(const char* new_name, std::size_t new_name_length) {
		std::memcpy(name, new_name, new_name_length);
		name[new_name_length] = '\0';
	}
	
	int get_id() const {
		return id_;
	}
	
private:
	char name[MAX_NAME_LENGTH + 1];
	int id_;
	bool valid_;
};

class chat_server {
public:
	chat_server(boost::asio::io_context& io_context, std::size_t port)
	  : server_(io_context, port, std::bind(&chat_server::handle_accept, this, std::placeholders::_1, std::placeholders::_2),
	    std::bind(&chat_server::handle_read, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
	{}
	
	void handle_accept(std::size_t client_id, bool connect) {
		std::scoped_lock lock(clients_mutex_);
		if (connect) {
			clients_.emplace_back(client_id);
			printw("New client connected with id: %d\n", client_id);
			std::stringstream ss;
			ss << "server: " << "New client connected with id " << client_id << ".";
			const std::string& tmp = ss.str();
			const char* reply = tmp.c_str();
			server_.send_to_all_except(client_id, reply, tmp.length());
			refresh();
		} else {
			clients_.remove_if([this,client_id](const client &client_){ 
				if (client_.get_id() == client_id) {
					printw("Client %d (%s) disconnected.\n", client_id, client_.get_name());
					refresh();
					std::stringstream ss;
					ss << "server: " << client_.get_name() << " has disconnected.";
					const std::string& tmp = ss.str();
					const char* reply = tmp.c_str();
					server_.send_to_all(reply, tmp.length());
					return true;
				} else {
					return false;
				}
			});
		}
		//std::cout << "New client with id: " << client_id << std::endl;
	}
	
	void handle_read(std::size_t sender, char* body, std::size_t length) {
		// whenever the server receives a message, this function will be called
		// where clients[sender] will be the connection that sent the message
		// body will be a pointer to the start of the body of the message
		// and length will be the length of the body 
		// we will process the message here and decide if / what to send in response
		// (for example, in a chat server, we'd want to forward the message to every client
		// with the name of the sender attached to it so that clients can update the chat dialogue)
		if (body[0] == '#') {
			// could be a command that we need to process
			if (!strncmp(body, "#name ", 6)) {
				// user requesting a name change
				// let's make sure the name they want is valid
				char* name = body + 6;
				std::size_t name_length = strlen(name);
				if (name_length == 0) {
					printw("Client %u attempted to change their name to an empty string which is not allowed.\n", sender);
					char reply[] = "server: Cannot change your name to the empty string";
					server_.send_to(sender, reply, strlen(reply));
				} else if (name_length > MAX_NAME_LENGTH) {
					printw("Client %u attempted to change their name to a name that is too long.\n", sender);
					std::stringstream ss;
					ss << "server: Name cannot exceed " << MAX_NAME_LENGTH << " characters.";
					const std::string& tmp = ss.str();
					const char* reply = tmp.c_str();
					server_.send_to(sender, reply, tmp.length());
				} else {
					bool valid = true;
					for (int i = 0; i < name_length; i++) {
						if (!isalpha(name[i]) && !isdigit(name[i])) {
							printw("Client %u attempted to change their name to a name with at least one non-alphanumeric character.\n", sender);
							char reply[] = "server: Names can only contain letters and numbers.";
							server_.send_to(sender, reply, strlen(reply));
							valid = false;
							break;
						}
					}
					for (auto& client_ : clients_) {
						if (!strcmp(name, client_.get_name())) {
							printw("Client %u attempted to change their name to a name already in use by client %u.\n", sender, client_.get_id());
							char reply[] = "server: Name change declined due to name already in use.";
							server_.send_to(sender, reply, strlen(reply));
							valid = false;
							break;
						}
					}
					if (valid) {
						for (auto& client_ : clients_) {
							if (client_.get_id() == sender) {
								printw("Client %u has changed their name to %s.\n", sender, name);
								std::stringstream ss;
								ss << "server: " << client_.get_name() << " has changed their name to " << name << ".";
								const std::string& tmp = ss.str();
								const char* reply = tmp.c_str();
								client_.set_name(name, name_length);
								server_.send_to_all(reply, tmp.length());
								break;
							}
						}
					}
				}
				refresh();
			} else if (!strncmp(body, "#msg ", 5)) {
				// #msg <target-name> <message>
				// client trying to send a private message to another client
				// first let's make sure they formatted the command properly
				// while extracting the name of the client they are attempting to message
				printw("Client %u: %s\n", sender, body);
				std::size_t name_start = 5;
				std::size_t name_end = 5;
				while (name_end < length && body[name_end] != ' ')
					name_end++;
				if (name_end == length || name_start+1 == name_end) {
					printw("Client %u attempted to send a message, but didn't use the command properly.\n", sender);
					char reply[] = "server: Command not executed properly. Must be #msg <target-name> <message>.";
					server_.send_to(sender, reply, strlen(reply));
				} else {
					std::size_t name_length = name_end - name_start;
					char name[name_length+1];
					std::memcpy(name, body+name_start, name_length);
					name[name_length] = '\0';
					bool found = false;
					for (auto& client_ : clients_) {
						// search for client with that name
						if (!strcmp(name, client_.get_name())) {
							// found client so let's send them the message
							// first, we need to get the name of the sender
							for (auto& client2_ : clients_) {
								if (client2_.get_id() == sender) {
									std::stringstream ss;
									ss << client2_.get_name() << " (to " << client_.get_name() << "): " << body+name_end+1;
									const std::string& tmp = ss.str();
									const char* reply = tmp.c_str();
									server_.send_to(client_.get_id(), reply, tmp.length());
									server_.send_to(sender, reply, tmp.length());
									break;
								}
							}
							found = true;
							break;
						}
					}
					if (!found) {
						// unable to find a client with the name specified by the msg command
						char reply[] = "server: Unable to find a client with the name you specified.";
						printw("Unable to find a client with the name specified in the #msg command.\n");
						server_.send_to(sender, reply, strlen(reply));
					}
				}
				refresh();
			} else if (!strncmp(body, "#clients", 8)) {
				// client requesting a list of the clients
				std::stringstream ss;
				ss << "\n";
				for (auto& client_ : clients_) {
					ss << client_.get_name() << "\n";
				}
				const std::string& tmp = ss.str();
				const char* reply = tmp.c_str();
				server_.send_to(sender, reply, tmp.length());
			}
			return;
		}
		for (auto& client_ : clients_) {
			if (client_.get_id() == sender) {
				std::size_t sender_name_len = strlen(client_.get_name());
				std::size_t new_message_length = sender_name_len + length + 3;
				char new_message[new_message_length];
				
				sprintf(new_message, "%s: ", client_.get_name());
				memcpy(new_message + sender_name_len + 2, body, length);
				new_message[new_message_length - 1] = '\0';
				
				attron(A_BOLD);
				for (int i = 0; i < new_message_length-1; i++) {
					if (new_message[i] == ':') attroff(A_BOLD);
					addch(new_message[i]);
				}
				addch('\n');
				refresh();
				
				server_.send_to_all(new_message, new_message_length-1);
			}
		}
	}
private:
	net_server server_;
	std::list<client> clients_;
	std::mutex clients_mutex_;
};

int main() {
	try {
		initscr();
		scrollok(stdscr, TRUE);
		boost::asio::io_context io_context;
		chat_server serv(io_context, 1234);
		io_context.run();
	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
		endwin();
	}
	
	return 0;
}