#include "net_client.hpp"
#include "chat_constants.hpp"

#include <iostream>
#include <cstring>
#include <ncurses.h>

/*

A client-side application for a chatroom.
This application uses the net_client class to handle its networking.
The application just needs to provide the net_client object with one function:
This function will get called anytime the client receives a message.

For this application, the only messages that get received by clients, are
messages that are meant to be displayed in the chatroom window so as soon as a 
message is received, it is printed to the output window.

The ncurses library is used to split the terminal into separate output and input windows.
Without these separate windows, if you were in the middle of typing input when a message
was received, the input would get fractured by the output. For example if you were 
trying to type 'hello', but had only typed 'hel' so far when a message 'goodbye' was received,
the terminal would display:

...
helgoodbye
lo
...

Ncurses also has the added benefit of letting us format output with colors to make the messages
a little easier to read.

The client has a small number of commands available to it and they can be viewed
by submitting "#help" in the input window. 
For example, there are commands to change the client's name, clear the output window,
send a private message to another client, view the client list, and exit gracefully.

*/

class chat_client {
public:
	chat_client(boost::asio::io_context& io_context, std::size_t port) 
	  : io_context_(io_context), client_(io_context, port,
	      std::bind(&chat_client::handle_read, this, std::placeholders::_1, std::placeholders::_2)) {
		max_body_length_ = client_.get_max_body_length() - MAX_NAME_LENGTH;
		
		std::size_t input_win_h = LINES / 8;
		if (input_win_h < 3) input_win_h = 3;
		std::size_t output_win_h = LINES - input_win_h - 1;
		
		output_win = newwin(output_win_h, COLS, 0, 0);
		input_win = newwin(input_win_h, COLS, output_win_h+1, 0);
		scrollok(output_win, TRUE);
		scrollok(input_win, TRUE);
		refresh();
		
		reset_input();
	}
	
	~chat_client() {
		werase(output_win);
		werase(input_win);
		wrefresh(output_win);
		wrefresh(input_win);
		delwin(output_win);
		delwin(input_win);
	}
	
	void start() {
		write_loop();
	}
	
private:
	void write_loop() { 
		char message[max_body_length_];
		wgetnstr(input_win, message, max_body_length_);
		reset_input();
		
		if (message[0] == '#') {
			if (!strcmp(message, "#help")) {
				print_help();
			} else if (!strcmp(message, "#clear")) {
				werase(output_win);
				wrefresh(output_win);
				wrefresh(input_win);
			} else if (!strncmp(message, "#name ", 6)) {
				client_.send(message, strlen(message));
			} else if (!strncmp(message, "#msg ", 5)) {
				// the message should be structured like
				// #msg <client-name> <message>
				client_.send(message, strlen(message));
			} else if (!strcmp(message, "#exit")) {
				wprintw(output_win, "Exiting.\n");
				io_context_.stop();
				return;
			} else if (!strcmp(message, "#clients")) {
				client_.send(message, strlen(message));
			} else {
				wprintw(output_win, "Command \"%s\" not recognized.\n", message);
				wrefresh(output_win);
			}
		} else {
			client_.send(message, strlen(message));
		}
		
		write_loop();
	}
	
	void reset_input() {
		werase(input_win);
		wprintw(input_win, "For a list of available commands, type (and submit) #help.\nInput: ");
		wrefresh(input_win);
	}
	
	void print_help() {
		// name change
		wattron(output_win, A_BOLD);
		wattron(output_win, COLOR_PAIR(2));
		wprintw(output_win, "#name <name>: ");
		wattroff(output_win, A_BOLD);
		wattroff(output_win, COLOR_PAIR(2));
		wprintw(output_win, "Changes your name to <name>.\n");
		
		// exit
		wattron(output_win, A_BOLD);
		wattron(output_win, COLOR_PAIR(2));
		wprintw(output_win, "#exit: ");
		wattroff(output_win, A_BOLD);
		wattroff(output_win, COLOR_PAIR(2));
		wprintw(output_win, "Disconnects you from the server.\n");
		
		// clear
		wattron(output_win, A_BOLD);
		wattron(output_win, COLOR_PAIR(2));
		wprintw(output_win, "#clear: ");
		wattroff(output_win, A_BOLD);
		wattroff(output_win, COLOR_PAIR(2));
		wprintw(output_win, "Clears the current output.\n");
		
		// message specific client
		wattron(output_win, A_BOLD);
		wattron(output_win, COLOR_PAIR(2));
		wprintw(output_win, "#msg <client_name> <message>: ");
		wattroff(output_win, A_BOLD);
		wattroff(output_win, COLOR_PAIR(2));
		wprintw(output_win, "Sends <message> to <client_name> if a client with that name is currently connected.\n");
		
		// request client list
		wattron(output_win, A_BOLD);
		wattron(output_win, COLOR_PAIR(2));
		wprintw(output_win, "#clients: ");
		wattroff(output_win, A_BOLD);
		wattroff(output_win, COLOR_PAIR(2));
		wprintw(output_win, "Lists all currently connected clients.\n");
		
		wrefresh(output_win);
		wrefresh(input_win);
	}
	
	void handle_read(char* body, std::size_t length) {
		wattron(output_win, A_BOLD);
		wattron(output_win, COLOR_PAIR(1));
		for (int i = 0; i < length; i++) {
			if (body[i] == ':') {
				wattroff(output_win, A_BOLD);
				wattroff(output_win, COLOR_PAIR(1));
			}
			waddch(output_win, body[i]);
		}
		waddch(output_win, '\n');
		wrefresh(output_win);
		wrefresh(input_win);
	}

	WINDOW *output_win;
	WINDOW *input_win;
	net_client client_;
	std::size_t max_body_length_;
	boost::asio::io_context& io_context_;
};

int main() {
	try {
		initscr();
		start_color();
		init_pair(1, COLOR_MAGENTA, COLOR_BLACK); // color for client names in chat
		init_pair(2, COLOR_CYAN, COLOR_BLACK); // color for help message
		boost::asio::io_context io_context;
		{
			chat_client client(io_context, 1234);
			
			std::thread io_thread([&io_context](){ io_context.run(); });
			client.start();
			
			io_thread.join();
		} // so that the client destructor gets called once io_thread.join() returns
		
		endwin();
	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
		endwin();
	}
	
	return 0;
}