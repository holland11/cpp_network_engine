#include "net_server.hpp"

#include <sstream>
#include <ncurses.h>

/*

A server-side application for a connect4 game.
This application uses the net_server class to handle its networking.

Whenever a client connects, the server will search all other clients to see
if there is another client not currently in a game. If such a client exists,
a game will be started.

The game state is only maintained on the server-side, so the server needs
to send the entire state of the game board to both clients after each turn.

After each turn is processed, the server will check to see if the game is finished
(either due to a player winning, or the game ending in a draw) and the clients
will be notified accordingly.

If a client disconnects mid-game, their opponent will be put back into the queue
of players waiting for an opponent.

*/

class player {
public:
	player(std::size_t id)
	  : id_(id) {
	}
	
	std::size_t get_id() { return id_; }
	
	bool in_game;
private:
	std::size_t id_;
};

class game {
public:
	enum tile { empty=0, x=1, o=2 };
	enum { rows = 6 }; // this value must be a single digit value
	enum { cols = 7 }; // this value must be a single digit value

	game(player* p1, player* p2, net_server* server_ptr_)
	  : players{p1, p2}, server_ptr(server_ptr_), turn('1') {
		start_color();
		init_pair(1, COLOR_MAGENTA, COLOR_BLACK);
		init_pair(2, COLOR_CYAN, COLOR_BLACK);
	}
	
	void start() {
		// send a message to both players that the game is
		// starting and tell them which player they are and 
		// what the board dimensions are
		clear_board();
		std::size_t msg_len = snprintf(NULL, 0, "#start 1 %u %u", rows, cols);
		
		char p1_start_msg[msg_len];
		char p2_start_msg[msg_len];
		sprintf(p1_start_msg, "#start 1 %u %u", rows, cols);
		sprintf(p2_start_msg, "#start 2 %u %u", rows, cols);
		
		server_ptr->send_to(players[0]->get_id(), p1_start_msg, strlen(p1_start_msg));
		server_ptr->send_to(players[1]->get_id(), p2_start_msg, strlen(p2_start_msg));
	}
	
	void clear_board() {
		for (int y = 0; y < rows; y++) {
			for (int x = 0; x < cols; x++) {
				board[y][x] = empty;
			}
		}
	}
	
	void draw_board(WINDOW* win) {
		for (int x = 0; x < cols; x++) {
			wprintw(win, "-~");
		}
		waddch(win, '\n');
		for (int y = 0; y < rows; y++) {
			for (int x = 0; x < cols; x++) {
				waddch(win, '|');
				draw_tile(win, board[y][x]);
			}
			wprintw(win, "|\n");
		}
		for (int x = 0; x < cols; x++) {
			wprintw(win, "-~");
		}
		waddch(win, '-');
	}
	
	player** get_players() {
		return players;
	}
	
	char get_turn() {
		return turn;
	}
	
	void toggle_turn() {
		if (turn == '1')
			turn = '2';
		else
			turn = '1';
	}
	
	tile (*get_board())[cols] {
		return board;
	}
	
	void apply_move(char player_num, std::size_t row, std::size_t col) {
		if (player_num == '1')
			board[row][col] = x;
		else
			board[row][col] = o;
	}
	
	bool check_for_win(char player_num) {
		// returns true if player_num has won the game
		// algorithm taken from https://stackoverflow.com/questions/32770321/connect-4-check-for-a-win-algorithm/32771681
		tile tile_ = x;
		if (player_num == '2')
			tile_ = o;
		
		// horizontal check
		for (int j = 0; j < rows-3; j++) {
			for (int i = 0; i < cols; i++) {
				if (board[i][j] == tile_ && board[i][j+1] == tile_ && board[i][j+2] == tile_ && board[i][j+3] == tile_)
					return true;
			}
		}
		
		// vertical check
		for (int i = 0; i < cols-3; i++) {
			for (int j = 0; j < rows; j++) {
				if (board[i][j] == tile_ && board[i+1][j] == tile_ && board[i+2][j] == tile_ && board[i+3][j] == tile_)
					return true;
			}
		}
		
		// bottom left to top right diagonal
		for (int i = 3; i < cols; i++) {
			for (int j = 0; j < rows-3; j++) {
				if (board[i][j] == tile_ && board[i-1][j+1] == tile_ && board[i-2][j+2] == tile_ && board[i-3][j+3] == tile_)
					return true;
			}
		}
		
		// top left to bottom right diagonal
		for (int i = 3; i < cols; i++) {
			for (int j = 3; j < rows; j++) {
				if (board[i][j] == tile_ && board[i-1][j-1] == tile_ && board[i-2][j-2] == tile_ && board[i-3][j-3] == tile_)
					return true;
			}
		}
		
		return false;
	}
	
	bool check_for_draw() {
		// Precondition: game has not been won (make sure to check check_for_win() first)
		// game is a draw if the first row is completely used up
		for (int i = 0; i < cols; i++) {
			if (board[0][i] == empty)
				return false;
		}
		return true;
	}
	
	void game_over() { turn = '0'; }
	
private:
	void draw_tile(WINDOW* win, tile& tile_) {
		if (tile_ == empty)
			waddch(win, ' ');
		else if (tile_ == x) {
			wattron(win, COLOR_PAIR(1));
			waddch(win, 'X');
			wattroff(win, COLOR_PAIR(1));
		}
		wattron(win, COLOR_PAIR(2));
		waddch(win, 'O');
		wattroff(win, COLOR_PAIR(2));
	}
	
	net_server* server_ptr;
	player* players[2];
	tile board[rows][cols];
	char turn;
};

class connect4_server {
public:
	connect4_server(boost::asio::io_context& io_context, std::size_t port)
	  : server_(io_context, port, std::bind(&connect4_server::handle_accept, this, std::placeholders::_1, std::placeholders::_2),
	    std::bind(&connect4_server::handle_read, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
	{}
private:
	void handle_accept(std::size_t client_id, bool connect) {
		if (connect) {
			for (auto& player_ : players_) {
				if (!player_.in_game) {
					// found a player for the new client to play against
					std::unique_lock lock(players_mutex_);
					players_.emplace_back(client_id);
					player& new_player = players_.back();
					player_.in_game = true;
					new_player.in_game = true;
					lock.unlock();
					// let's creat a new game!
					std::shared_ptr<game> new_game = std::make_shared<game>(&player_, &new_player, &server_);
					games_.push_back(new_game);
					new_game->start();
					char reply[] = "#msg s Your game has begun.";
					server_.send_to(client_id, reply, strlen(reply));
					server_.send_to(player_.get_id(), reply, strlen(reply));
					printw("New client connected with id %u, starting a game with client %u.\n", client_id, player_.get_id());
					refresh();
					return;
				}
			}
			std::unique_lock lock(players_mutex_);
			players_.emplace_back(client_id);
			player& new_player = players_.back();
			lock.unlock();
			// no players available to start a new game so we wait
			char reply[] = "#msg s No players available to start a new game. You will be put in a game when a new player joins.";
			server_.send_to(client_id, reply, strlen(reply));
			printw("New client connected with id %u, but no player available to start a new game.\n", client_id);
			refresh();
			return;
		} else {
			// client disconnected so we should check to see if they were in a game
			// if they were in a game, let's end the game and put their opponent back 
			// into the pool of players waiting for an opponent
			// we should also check to see if there is already an opponent waiting
			// in which case we can start a new game between them
			player* other_player = nullptr;
			std::unique_lock lock(players_mutex_);
			for (auto it = players_.begin(); it != players_.end(); ++it) {
				player& player_ = *it;
				if (client_id == player_.get_id()) {
					if (player_.in_game) {
						// find the game
						for (int i = 0; i < games_.size(); i++) {
							std::shared_ptr<game> game_ = games_[i];
							if (game_->get_players()[0]->get_id() == player_.get_id()) {
								other_player = game_->get_players()[1];
							} else if (game_->get_players()[1]->get_id() == player_.get_id()) {
								other_player = game_->get_players()[0];
							}
							if (other_player) {
								other_player->in_game = false;
								char reply1[] = "#endgame";
								server_.send_to(other_player->get_id(), reply1, strlen(reply1));
								char reply2[] = "#msg s Your opponent has disconnected so you have been put back in "
												"queue to wait for a new opponent.";
								server_.send_to(other_player->get_id(), reply2, strlen(reply2));
								games_.erase(games_.begin() + i);
								break;
							}
						}
					}
					printw("Player %u has disconnected.\n", client_id);
					refresh();
					players_.erase(it);
					break;
				}
			}
			lock.unlock();
			// if other_player is not nullptr, then that means 
			// that the user disconnecting was in a game
			// their game has been removed from the list of games
			// and other_player->in_game is now false
			// but we should check to see if there was already a user waiting to play a game
			// in which case we can start them in a game right now
			lock.lock();
			for (auto& player_ : players_) {
				if (player_.get_id() == other_player->get_id()) continue;
				if (!player_.in_game) {
					// found a player for the new client to play against
					player_.in_game = true;
					other_player->in_game = true;
					lock.unlock();
					// let's creat a new game!
					std::shared_ptr<game> new_game = std::make_shared<game>(&player_, other_player, &server_);
					games_.push_back(new_game);
					new_game->start();
					printw("Starting a game between client %u and client %u.\n", other_player->get_id(), player_.get_id());
					refresh();
					return;
				}
			}
			lock.unlock();
		}
	}
	
	void handle_read(std::size_t sender, char* body, std::size_t length) {
		// a message that was sent by a client with id sender
		// first let's find the player object
		player* player_ptr = nullptr;
		for (auto& player_ : players_) {
			if (player_.get_id() == sender) {
				player_ptr = &player_;
				break;
			}
		}
		if (player_ptr == nullptr)
			return;
		if (!player_ptr->in_game)
			return; // ignore message since player isn't in a game right now
		// let's find the game that the player is a part of
		std::shared_ptr<game> game_ptr;
		player* other_player_ptr = nullptr;
		char player_num = '0';
		for (auto& game_ : games_) {
			if (game_->get_players()[0]->get_id() == sender) {
				game_ptr = game_;
				player_num = '1';
				other_player_ptr = game_->get_players()[1];
				break;
			} else if (game_->get_players()[1]->get_id() == sender) {
				game_ptr = game_;
				player_num = '2';
				other_player_ptr = game_->get_players()[0];
				break;
			}
		}
		if (!game_ptr) {
			printw("Error: Player in_game = true, yet can't find a game with the player in it.\n");
			refresh();
			return;
		}
		if (!strncmp(body, "#msg ", 5)) {
			// sender has sent a message that we need to forward to their opponent
			// first, we need to process the message a little bit
			std::stringstream ss;
			ss << "#msg " << player_num << " " << body+5;
			const std::string& tmp = ss.str();
			const char* reply = tmp.c_str();
			server_.send_to(other_player_ptr->get_id(), reply, tmp.length());
			server_.send_to(sender, reply, tmp.length());
		} else if (isdigit(body[0])) {
			// player submitting a move for their game
			// let's check to make sure that it's their turn
			// and that the move is valid
			if (player_num != game_ptr->get_turn()) {
				printw("Client %u attempted a move when it wasn't their turn.\n", sender);
				char reply[] = "#msg s It is not your turn to make a move.";
				server_.send_to(sender, reply, strlen(reply));
				refresh();
				return;
			} else {
				std::size_t move = body[0] - '0';
				if (move >= game_ptr->cols) {
					// move out of bounds
					printw("Client %u has attempted a move that is out of bounds.\n", sender);
					char reply[] = "#msg s The move you have chosen is out of bounds.\n";
					server_.send_to(sender, reply, strlen(reply));
					refresh();
					return;
				}
				// now make sure there is room in that column
				game::tile (*board)[game::cols] = game_ptr->get_board();
				if (board[0][move] != game::tile::empty) {
					// no room left in that column
					printw("Client %u has attempted a move on a full column.\n", sender);
					char reply[] = "#msg s The column you have chosen is already full.\n";
					server_.send_to(sender, reply, strlen(reply));
					refresh();
					return;
				}
				// execute the move
				// then check to see if the game is over
				for (int i = game_ptr->rows-1; i >= 0; i--) {
					// look for the lowest empty cell in the chosen column
					if (board[i][move] == game::tile::empty) {
						// this is where the move will go
						game_ptr->apply_move(player_num, i, move);
						game_ptr->toggle_turn();
						bool game_won = game_ptr->check_for_win(player_num);
						bool game_draw = game_ptr->check_for_draw();
						
						// let's send each player the current board state
						std::stringstream ss;
						
						if (game_won) {
							game_ptr->game_over();
							ss << "#win " << player_num << " ";
						} else if (game_draw) {
							game_ptr->game_over();
							ss << "#draw ";
						} else {
							ss << "#turn " << game_ptr->get_turn() << " ";
						}
						
						for (int row = 0; row < game_ptr->rows; row++) {
							for (int col = 0; col < game_ptr->cols; col++) {
								if (board[row][col] == game::tile::empty)
									ss << ' ';
								else if (board[row][col] == game::tile::x)
									ss << 'x';
								else
									ss << 'o';
							}
						}
						
						const std::string& tmp = ss.str();
						const char* reply = tmp.c_str();
						server_.send_to(sender, reply, tmp.length());
						server_.send_to(other_player_ptr->get_id(), reply, tmp.length());
						
						printw("Client %u move processed.\n", sender);
						
						break;
					}
				}
			}
			refresh();
		}
	}

	net_server server_;
	std::vector<std::shared_ptr<game>> games_;
	std::list<player> players_;
	std::mutex players_mutex_;
};

int main() {
	try {
		initscr();
		scrollok(stdscr, TRUE);
		boost::asio::io_context io_context;
		connect4_server serv(io_context, 1234);
		io_context.run();
	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
		endwin();
	}
	
	return 0;
}