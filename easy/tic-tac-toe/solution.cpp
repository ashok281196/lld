#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>

using namespace std;

enum class Mark { EMPTY, X, O };
enum class GameStatus { IN_PROGRESS, WIN, DRAW };

struct Player {
    int id;
    string name;
    Mark mark;
};

struct Cell {
    Mark mark = Mark::EMPTY;
    bool isEmpty() const { return mark == Mark::EMPTY; }
};

class Board {
public:
    Board(int n) {
        size_        = n;
        filled_      = 0;
        diagSum_     = 0;
        antiDiagSum_ = 0;
        grid_.assign(n, vector<Cell>(n));  // n x n empty grid
        rowSum_.assign(n, 0);                    // one running tally per row
        colSum_.assign(n, 0);                    // one running tally per column
    }

    int  size() const { return size_; }
    bool inBounds(int r, int c) const {
        return r >= 0 && r < size_ && c >= 0 && c < size_;
    }
    bool isEmpty(int r, int c) const { return grid_[r][c].isEmpty(); }
    bool isFull()  const { return filled_ == size_ * size_; }

    // Place mark, return true if THIS move completed a line. O(1).
    bool place(int r, int c, Mark mark) {
        grid_[r][c].mark = mark;
        ++filled_;

        int d      = (mark == Mark::X) ?  1     : -1;      // signed delta
        int target = (mark == Mark::X) ?  size_ : -size_;  // full line value

        rowSum_[r] += d;
        colSum_[c] += d;
        if (r == c)             diagSum_     += d;  // main diagonal
        if (r + c == size_ - 1) antiDiagSum_ += d;  // anti-diagonal

        return rowSum_[r] == target
            || colSum_[c] == target
            || (r == c             && diagSum_     == target)
            || (r + c == size_ - 1 && antiDiagSum_ == target);
    }

private:
    int size_;
    vector<vector<Cell>> grid_;
    vector<int> rowSum_, colSum_;
    int diagSum_, antiDiagSum_;
    int filled_;
};

// ---------------------------------------------------------------------------
// Game: rule engine. validate -> mutate -> transition.
// ---------------------------------------------------------------------------
class Game {
public:
    // board_(boardSize) stays in the init list: Board has no default ctor.
    Game(int boardSize, vector<Player> players) : board_(boardSize) {
        if (players.size() < 2)
            throw invalid_argument("need at least 2 players");
        players_ = players;
        turnIdx_ = 0;
        status_  = GameStatus::IN_PROGRESS;
        winner_  = nullptr;
    }

    void makeMove(int playerId, int row, int col) {
        if (status_ != GameStatus::IN_PROGRESS)
            throw logic_error("game is already over");

        Player& current = players_[turnIdx_];
        if (current.id != playerId)
            throw invalid_argument("not this player's turn");
        if (!board_.inBounds(row, col))
            throw invalid_argument("move out of bounds");
        if (!board_.isEmpty(row, col))
            throw invalid_argument("cell already occupied");

        bool won = board_.place(row, col, current.mark);

        if (won) {
            status_ = GameStatus::WIN;
            winner_ = &current;
        } else if (board_.isFull()) {
            status_ = GameStatus::DRAW;
        } else {
            turnIdx_ = (turnIdx_ + 1) % players_.size();  // round-robin
        }
    }

    GameStatus    getStatus() const { return status_; }
    const Player* getWinner() const { return winner_; }

private:
    Board board_;
    vector<Player> players_;
    int turnIdx_;
    GameStatus status_;
    Player* winner_;
};

// ---------------------------------------------------------------------------
// Driver: happy path (win) + an illegal move.
// ---------------------------------------------------------------------------
int main() {
    vector<Player> players = {
        {1, "Alice", Mark::X},
        {2, "Bob",   Mark::O},
    };
    Game game(3, players);

    // X takes the top row -> X wins.
    game.makeMove(1, 0, 0);
    game.makeMove(2, 1, 0);
    game.makeMove(1, 0, 1);
    game.makeMove(2, 1, 1);
    game.makeMove(1, 0, 2);   // top row complete

    if (game.getStatus() == GameStatus::WIN)
        cout << "Winner: " << game.getWinner()->name << "\n";  // Alice

    // Game over -> next move is rejected, not crashed.
    try {
        game.makeMove(2, 2, 2);
    } catch (const exception& e) {
        cout << "Rejected: " << e.what() << "\n";  // game is already over
    }
    return 0;
}
