#include "net_client.hpp"

#include <iostream>
#include <cstring>
#include <ncurses.h>

/*

A client-side application for a connect4 game.
The application uses the net_client library to handle its networking.

The ncurses library is used to split the terminal up into 3 separate windows.
One window for client input, one window for chat / server messages, and one
window to display the game board.

The ncurses library also allows us to change the color of our output
which is especially helpful for being able to read the gameboard more easily.

Instead of maintaining the state of the game on both the client and server sides,
this application just has the server maintain the state of the game and it is sent to the 
clients after each turn.
This wouldn't be a good idea for any real-time games, and it's far from optimal
from the perspective of network bandwidth, but it works well enough for the purpose
of demonstrating / testing / fine-tuning the net_client/net_server library.

When a client connects, they will be forced to wait until another client connects
at which point a game will begin and both clients will be notified.

When a game ends, there is currently no way for the clients to rematch each other
so they will have to CTRL+C to exit the program and then start it again.
It wouldn't be very difficult to include such functionality, but I don't have much time
and the main purpose of my project was the net_server / net_client classes 
so I'm not too concerned with the chatroom or connect4 games being super polished.

Besides making game moves, clients can also send messages to their opponent
using the #msg command. The client can submit "#help" for instructions on how
to use the #msg command.

*/

class connect4_client {
public:
	connect4_client(boost::asio::io_context& io_context, std::size_t port) 
	  : client_(io_context, port,
	      std::bind(&connect4_client::handle_read, this, std::placeholders::_1, std::placeholders::_2)) {
		max_body_length_ = client_.get_max_body_length() - 10;
		
		std::size_t input_win_h = LINES / 8;
		if (input_win_h < 3) input_win_h = 3;
		std::size_t chat_win_h = LINES - input_win_h - 1;
		std::size_t chat_win_w = (COLS / 2)-1;
		
		game_win = newwin(chat_win_h, chat_win_w, 0, 0);
		chat_win = newwin(chat_win_h, chat_win_w, 0, chat_win_w+1);
		input_win = newwin(input_win_h, COLS, chat_win_h+1, 0);
		scrollok(chat_win, TRUE);
		scrollok(input_win, TRUE);
		refresh();
		
		reset_input();
	}
	
	~connect4_client() {
		delwin(chat_win);
		delwin(game_win);
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
			} else if (!strncmp(message, "#msg ", 5)) {
				client_.send(message, strlen(message));
			} else {
				wprintw(chat_win, "Command \"%s\" not recognized.\n", message);
				wrefresh(chat_win);
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
		// chat message
		wprintw(chat_win, "\n");
		
		wattron(chat_win, A_BOLD);
		wattron(chat_win, COLOR_PAIR(2));
		wprintw(chat_win, "#msg <message>: ");
		wattroff(chat_win, A_BOLD);
		wattroff(chat_win, COLOR_PAIR(2));
		wprintw(chat_win, "Sends <message> to your current opponent.\n");
		
		wattron(chat_win, A_BOLD);
		wattron(chat_win, COLOR_PAIR(2));
		wprintw(chat_win, "<number>: ");
		wattroff(chat_win, A_BOLD);
		wattroff(chat_win, COLOR_PAIR(2));
		wprintw(chat_win, "To make a game move, submit the number of the column you'd like to drop your piece in.\n");
		
		wprintw(chat_win, "To ");
		wattron(chat_win, A_BOLD);
		wattron(chat_win, COLOR_PAIR(3));
		wprintw(chat_win, "close");
		wattroff(chat_win, A_BOLD);
		wattroff(chat_win, COLOR_PAIR(3));
		wprintw(chat_win, " the game, press ");
		wattron(chat_win, A_BOLD);
		wattron(chat_win, COLOR_PAIR(3));
		wprintw(chat_win, "CTRL+C");
		wattroff(chat_win, A_BOLD);
		wattroff(chat_win, COLOR_PAIR(3));
		wprintw(chat_win, ".\n");
		
		wprintw(chat_win, "\n");
		wrefresh(chat_win);
		wrefresh(input_win);
	}
	
	void handle_read(char* body, std::size_t length) {
		if (body[0] == '#') {
			if (!strncmp(body, "#msg ", 5)) {
				wattron(chat_win, A_BOLD);
				if (body[5] == '1') {
					wattron(chat_win, COLOR_PAIR(1));
					wprintw(chat_win, "Player 1: ");
					wattroff(chat_win, COLOR_PAIR(1));
				} else if (body[5] == '2') {
					wattron(chat_win, COLOR_PAIR(2));
					wprintw(chat_win, "Player 2: ");
					wattroff(chat_win, COLOR_PAIR(2));
				} else {
					wattron(chat_win, COLOR_PAIR(3));
					wprintw(chat_win, "Server: ");
					wattroff(chat_win, COLOR_PAIR(3));
				}
				for (int i = 7; i < length; i++) {
					waddch(chat_win, body[i]);
				}
				waddch(chat_win, '\n');
			} else if (!strncmp(body, "#start ", 7)) {
				// game start message
				// message is structured like: 
				// #start your_id num_rows num_cols
				// where your_id is either 1 or 2
				// player 1 always gets first turn
				your_id = body[7];
				game_rows = body[9] - '0';
				game_cols = body[11] - '0';
				
				draw_board(nullptr, true);
				
				if (your_id == '1') {
					wprintw(game_win, "It is your turn.\n");
				} else {
					wprintw(game_win, "You must wait for player 1 to make the first move.\n");
				}
			} else if (!strncmp(body, "#endgame", 8)) {
				// game has been terminated
				wprintw(game_win, "This game has been terminated.\n");
				wprintw(game_win, "Please wait for a new opponent at which point a new game will be created.\n");
			} else if (!strncmp(body, "#turn ", 6)) {
				// a move has been made and the board has been updated
				// the message is structured like:
				// #turn <whose turn it is now> <board>
				// where <board> is a sequence of ' ' 'x' 'o'
				// based on whether the tile is empty, X, or O
				draw_board(body+8, false);
				
				wprintw(game_win, "You are ");
				if (your_id == '1') {
					wattron(game_win, COLOR_PAIR(1));
					waddch(game_win, 'X');
					wattroff(game_win, COLOR_PAIR(1));
				} else {
					wattron(game_win, COLOR_PAIR(2));
					waddch(game_win, 'O');
					wattroff(game_win, COLOR_PAIR(2));
				}
				waddch(game_win, '\n');
				
				if (body[6] == your_id) {
					wprintw(game_win, "It is now your turn.\n");
				} else {
					wprintw(game_win, "It is your opponent's turn.\n");
				}
				wrefresh(game_win);
			} else if (!strncmp(body, "#win ", 5)) {
				// a move has been made and the game is now over
				// the message is structured like:
				// #win <who won> <board>
				// where <board> is the same as in the #turn message
				draw_board(body+7, false);
				
				char winner = body[5];
				if (your_id == winner) {
					wprintw(game_win, "You have won!.\n");
				} else {
					wprintw(game_win, "You have lost.\n");
				}
				wprintw(game_win, "To start a new game, you will need to restart the client.\n");
				wrefresh(game_win);
			} else if (!strncmp(body, "#draw ", 6)) {
				// game has ended in a draw
				draw_board(body+6, false);
				
				wprintw(game_win, "The game has ended in a draw.\n");
				wprintw(game_win, "To start a new game, you will need to restart the client.\n");
				wrefresh(game_win);
			}
		}
		
		wrefresh(game_win);
		wrefresh(chat_win);
		wrefresh(input_win);
	}
	
	void draw_board(char* board, bool empty) {
		werase(game_win);
		
		for (int col = 0; col < game_cols; col++) {
			wprintw(game_win, "-~");
		}
		wprintw(game_win, "-\n");
		
		for (int row = 0; row < game_rows; row++) {
			for (int col = 0; col < game_cols; col++) {
				waddch(game_win, '|');
				if (empty) {
					waddch(game_win, ' ');
				} else {
					char tile = *(board + col + row*game_cols);
					draw_tile(game_win, tile);
				}
			}
			wprintw(game_win, "|\n");
		}
		
		for (int col = 0; col < game_cols; col++) {
			wprintw(game_win, "-~");
		}
		wprintw(game_win, "-\n");
		
		for (int col = 0; col < game_cols; col++) {
			wprintw(game_win, " %d", col);
		}
		wprintw(game_win, "\n\n");
	}
	
	void draw_tile(WINDOW* win, char tile) {
		if (tile == ' ') {
			waddch(win, ' ');
		} else if (tile == 'x') {
			wattron(win, COLOR_PAIR(1));
			waddch(win, 'X');
			wattroff(win, COLOR_PAIR(1));
		} else {
			wattron(win, COLOR_PAIR(2));
			waddch(win, 'O');
			wattroff(win, COLOR_PAIR(2));
		}
	}
	
	std::size_t game_rows;
	std::size_t game_cols;
	char your_id;
	WINDOW *game_win;
	WINDOW *chat_win;
	WINDOW *input_win;
	net_client client_;
	std::size_t max_body_length_;
};

int main() {
	try {
		initscr();
		start_color();
		init_pair(1, COLOR_MAGENTA, COLOR_BLACK); // color for player 1
		init_pair(2, COLOR_CYAN, COLOR_BLACK); // color for player 2
		init_pair(3, COLOR_RED, COLOR_BLACK); // color for server
		boost::asio::io_context io_context;
		{
			connect4_client client(io_context, 1234);
			
			std::thread io_thread([&io_context](){ io_context.run(); });
			client.start();
			
			io_thread.join();
		} // so that client destructor gets called when io_thread.join() returns
		
		endwin();
	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
		endwin();
	}
	
	return 0;
}