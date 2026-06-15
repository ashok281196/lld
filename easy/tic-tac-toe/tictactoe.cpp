#include "tictactoe.hpp"

#include <stdexcept>

Board::Board(int n){
    size_ = n;
    filled_= 0;
    diagSum_ = 0;
    antiDiagSum_ = 0;
    grid_.assign(n, vector<Cell>(n));
    rowSum_.assign(n, 0);
    colSum_.assign(n, 0);
}

bool Board::inBounds(int r, int c) const{
    return r >= 0 && r < size_ && c >= 0 && c < size_;
}

bool Board::isEmpty(int r, int c) const{
   return grid_[r][c].isEmpty();
}

bool Board::isFull() const{
    return filled_ == size_ * size_;
}

bool Board::place(int r,int c, Mark mark){
    grid_[r][c].mark = mark;
    ++filled_;

    int d = (mark==Mark::X) ? 1 : -1;
    int target = (mark==Mark::X) ? size_ : -size_;

    rowSum_[r] += d;
    colSum_[c] += d;
    if(r == c) diagSum_ += d;
    if(r + c == size_ - 1) antiDiagSum_ += d;

    return rowSum_[r] == target 
    || colSum_[c] == target 
    || diagSum_ == target 
    || antiDiagSum_ == target;
}

Game::Game(int boardSize, vector<Player> players) : board_(boardSize){
    if(players.size() < 2)
        throw invalid_argument("At least 2 players required");
    players_ = players;
    turnIdx_ = 0;
    status_ = GameStatus::IN_PROGRESS;
    winner_ = nullptr; 
}

void Game :: makeMove(int playerId, int row, int col){
    if(status_ != GameStatus::IN_PROGRESS)
        throw invalid_argument("Game is not in progress");

    Player&  current = players_[turnIdx_];
    if(current.id != playerId)
        throw invalid_argument("Not this player's turn");
    if(!board_.inBounds(row, col))
        throw invalid_argument("Move out of bounds");
    if(!board_.isEmpty(row, col))
        throw invalid_argument("Cell is not empty");

    bool won = board_.place(row, col, current.mark);
    if(won){
        status_ = GameStatus::WIN;
        winner_ = &current;
    } else if(board_.isFull()){
        status_ = GameStatus::DRAW;
    } else {
        turnIdx_ = (turnIdx_ + 1) % players_.size();
    } 
}