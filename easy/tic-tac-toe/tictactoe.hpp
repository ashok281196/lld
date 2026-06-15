#pragma once 
#include <vector>
#include <string>

using namespace std;

enum class Mark{ EMPTY, X, O };
enum class GameStatus{ IN_PROGRESS, WIN, DRAW };

struct Player{
    int id;
    std::string name;
    Mark mark;
};

struct Cell{
    Mark mark = Mark::EMPTY;
    bool isEmpty() const { return mark == Mark::EMPTY; } 
};

class Board{
public: 
    Board(int n);
    int size() const { return size_; }
    bool inBounds(int r ,int c) const;
    bool isEmpty(int r, int c) const;
    bool isFull() const;
    bool place(int r, int c, Mark mark);

private:
    int size_;
    vector<vector<Cell>> grid_;
    vector<int> rowSum_, colSum_;
    int diagSum_, antiDiagSum_;
    int filled_;
};

class Game{
public:
    Game(int boardSize, vector<Player> players);
    void makeMove(int playerId, int row, int col);
    GameStatus getStatus() const { return status_; }
    const Player* getWinner() const { return winner_; }

private:
    Board board_;
    vector<Player> players_;
    int turnIdx_;
    GameStatus status_;
    const Player* winner_;
};