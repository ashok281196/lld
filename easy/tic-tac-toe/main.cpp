#include "tictactoe.hpp"

#include <iostream>
using namespace std;
int main(){
    vector<Player> players = {
        {1, "Alice", Mark::X},
        {2, "Bob", Mark::O}
    };

    Game game(3, players);
    game.makeMove(1, 0, 0); // Alice places X at (0, 0)
    game.makeMove(2, 0, 1); // Bob places O at (0, 1)
    game.makeMove(1, 1, 1); // Alice places X at (1, 1)
    game.makeMove(2, 0, 2); // Bob places O at (0, 2)
    game.makeMove(1, 2, 2); // Alice places X at (2, 2) and wins

    if(game.getStatus() == GameStatus::WIN)
        std::cout << "Winner: " << game.getWinner()->name << std::endl;
    else if(game.getStatus() == GameStatus::DRAW)
        std::cout << "Game is a draw!" << std::endl;

    return 0;
}