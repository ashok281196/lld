# Tic-Tac-Toe — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is not just code — it's the reasoning that makes the code obvious.

---

## Step 0 — Read the problem like an interviewer wrote it

Before any code, extract the **hard constraints** that secretly drive the whole design. From the statement:

1. *"Generalize cleanly to N×N — win condition must not be hardcoded to 3."* → Board size is a parameter. No magic `3` anywhere.
2. *"Do not rescan the entire board after every move… maintain counters, check in O(1)."* → This is **the** differentiator. A naive `O(N²)` rescan answer is a fail. We must design counters.
3. *"Reject invalid moves gracefully — no crash."* → Validation is a first-class responsibility, not an afterthought.
4. Follow-ups hint at the *seams*: pluggable win rule, >2 players, undo. We don't build them yet, but we leave room.

> **Thinking habit:** the non-functional constraints (O(1), N×N) usually decide your data structures. Mine them first.

---

## Step 1 — Find the nouns → these become your classes

Read the prose and circle the nouns: *game, board, cell, player, mark, move, win, draw*.

Group them into responsibilities:

| Class | Owns | Why it exists |
|-------|------|---------------|
| `Player` | id, name, mark (X/O) | identity of who is playing |
| `Cell` | what mark sits here (or empty) | smallest unit of board state |
| `Board` | grid of cells + the **O(1) win counters** | knows the geometry and detects wins |
| `Game` | players, turn order, status | orchestrates rules, validates moves |

> **Thinking habit:** one class = one reason to change. `Board` changes if win-detection changes; `Game` changes if turn rules change. Keep them apart.

---

## Step 2 — Write the public interface FIRST (the contract)

The statement already hands us the shape. Pin it down before internals — it forces you to decide *what the outside world sees* and keeps you honest.

```cpp
enum class GameStatus { IN_PROGRESS, WIN, DRAW };

class Game {
public:
    Game(int boardSize, std::vector<Player> players);
    void makeMove(int playerId, int row, int col);   // throws on invalid move
    GameStatus getStatus() const;
    const Player* getWinner() const;                  // nullptr if none yet
};
```

Decisions baked in here:
- **Invalid move → throw** (`std::invalid_argument`). Clean, testable, no silent corruption. (Return-code is also fine; pick one and be consistent.)
- `getWinner()` returns a pointer so "no winner" is naturally `nullptr`.

> **Thinking habit:** the interface is a promise. Lock the promise, then you're free to refactor everything behind it.

---

## Step 3 — Model the leaves: `Player` and `Cell`

Start with the things that have no dependencies. A mark is naturally a small enum; empty is a state, so include it.

```cpp
enum class Mark { EMPTY, X, O };

struct Player {
    int id;
    std::string name;
    Mark mark;
};
```

`Cell` is almost trivial — it's just a mark. For 3×3 you could skip the class, but keeping it makes "what's in a square" explicit and gives a home for future per-cell data (e.g. who played, when).

```cpp
struct Cell {
    Mark mark = Mark::EMPTY;
    bool isEmpty() const { return mark == Mark::EMPTY; }
};
```

> **Thinking habit:** build bottom-up. Leaf types with no dependencies first — they make the next layer easy to write.

---

## Step 4 — The key insight: O(1) win detection with counters

This is the heart of the problem. Spend real thought here.

**Naive idea:** after each move, scan the move's row, its column, and the two diagonals → `O(N)` per move. Acceptable but the statement explicitly asks for better, and it shows you understand the trick.

**The counter trick.** A player wins a line when *all N cells in that line hold the same mark*. Instead of recounting, keep a **running signed tally per line**:

- Map a mark to a delta: X → `+1`, O → `-1`.
- Keep arrays: `rowSum[N]`, `colSum[N]`, plus two scalars `diagSum` and `antiDiagSum`.
- On a move at `(r, c)` by a player whose delta is `d`:
  - `rowSum[r] += d`
  - `colSum[c] += d`
  - if `r == c`: `diagSum += d` (main diagonal)
  - if `r + c == N - 1`: `antiDiagSum += d` (anti-diagonal)
- After updating, if **any** of the four touched tallies equals `+N` (X filled the line) or `-N` (O filled the line) → that player just won.

Why it works: a line of N identical X's sums to `+N`; N identical O's sums to `-N`. Any mix can't reach `±N`. Each move touches at most 4 lines, each update and check is constant → **O(1) per move**.

> ⚠️ **The signed-sum trick is exactly the 2-player (X/O) shortcut.** It does *not* extend to 3+ players (you can't pick one delta per player and keep `±N` meaningful). For the >2-players follow-up, swap the signed sums for **per-line per-player count arrays** (`rowCount[r][markIdx]`): increment the mover's count, win when any touched line's count for that mark hits `N`. Still O(1). Mention this trade-off out loud — interviewers love that you saw the limit.

Let's encapsulate this in `Board`:

```cpp
class Board {
public:
    Board(int n) {
        size_       = n;
        filled_     = 0;
        diagSum_    = 0;
        antiDiagSum_ = 0;

        grid_.assign(n, std::vector<Cell>(n));  // build an n x n grid of empty cells
        rowSum_.assign(n, 0);                    // n zeros, one tally per row
        colSum_.assign(n, 0);                    // n zeros, one tally per column
    }

    int size() const { return size_; }
    bool inBounds(int r, int c) const {
        return r >= 0 && r < size_ && c >= 0 && c < size_;
    }
    bool isEmpty(int r, int c) const { return grid_[r][c].isEmpty(); }
    bool isFull() const { return filled_ == size_ * size_; }

    // Places the mark and reports whether THIS move completed a line.
    bool place(int r, int c, Mark mark) {
        grid_[r][c].mark = mark;
        ++filled_;

        int d = (mark == Mark::X) ? 1 : -1;   // 2-player signed delta
        int target = (mark == Mark::X) ? size_ : -size_;

        rowSum_[r] += d;
        colSum_[c] += d;
        if (r == c)               diagSum_     += d;
        if (r + c == size_ - 1)   antiDiagSum_ += d;

        return rowSum_[r] == target
            || colSum_[c] == target
            || (r == c              && diagSum_     == target)
            || (r + c == size_ - 1  && antiDiagSum_ == target);
    }

private:
    int size_;
    std::vector<std::vector<Cell>> grid_;
    std::vector<int> rowSum_, colSum_;
    int diagSum_, antiDiagSum_;
    int filled_;
};
```

> **Constructor style note.** The body above sets members with plain assignment, and `.assign(count, value)` fills a `std::vector` after it's created. You'll often see the same thing written as a *member initializer list* — `Board(int n) : size_(n), grid_(n, std::vector<Cell>(n)) { }` — which constructs each member directly before the body runs. Both produce the same object; the list form is slightly more efficient and is **required** for members that have no default constructor (e.g. `Game` holds a `Board`, so `Game`'s list keeps `board_(boardSize)`). Use whichever you find clearer; assignment-in-body is fine for plain ints and sized vectors.

Notice `Board` knows geometry and win-math but **nothing about turns or players' identities** — that's `Game`'s job. Clean separation.

> **Thinking habit:** when a constraint says "make it efficient," reach for *incremental state* — update a small summary on each change instead of recomputing from scratch.

---

## Step 5 — Orchestrate with `Game`: validate, move, advance turn

`Game` is the rule engine. Its `makeMove` must, in order:

1. Reject if game already over.
2. Reject if it's not this player's turn.
3. Reject out-of-bounds / occupied cell.
4. Place the mark (delegate to `Board`).
5. Update status: win (if `Board` says so) → draw (if board full) → else next turn.

Doing the checks **before** mutating state guarantees "no silent corruption."

```cpp
class Game {
public:
    // board_(boardSize) MUST stay here: Board has no default constructor,
    // so it can't be made empty and then assigned. Everything else is set in the body.
    Game(int boardSize, std::vector<Player> players) : board_(boardSize) {
        if (players.size() < 2)
            throw std::invalid_argument("need at least 2 players");

        players_ = players;
        turnIdx_ = 0;
        status_  = GameStatus::IN_PROGRESS;
        winner_  = nullptr;
    }

    void makeMove(int playerId, int row, int col) {
        if (status_ != GameStatus::IN_PROGRESS)
            throw std::logic_error("game is already over");

        Player& current = players_[turnIdx_];
        if (current.id != playerId)
            throw std::invalid_argument("not this player's turn");

        if (!board_.inBounds(row, col))
            throw std::invalid_argument("move out of bounds");
        if (!board_.isEmpty(row, col))
            throw std::invalid_argument("cell already occupied");

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

    GameStatus getStatus() const { return status_; }
    const Player* getWinner() const { return winner_; }

private:
    Board board_;
    std::vector<Player> players_;
    int turnIdx_;
    GameStatus status_;
    Player* winner_;
};
```

Two design wins to call out in an interview:
- **Round-robin turn** (`% players_.size()`) already generalizes to N players — turn logic didn't hardcode "2".
- **Status transitions live in one place.** Nobody outside `Game` can set `WIN`/`DRAW`, so state can't drift.

> **Thinking habit:** validate → mutate → transition, in that fixed order. Every game/state-machine LLD follows this rhythm.

---

## Step 6 — Prove it with a tiny driver

Always show a `main` that exercises a win, a rejected move, and (optionally) a draw. It doubles as your test.

```cpp
#include <iostream>

int main() {
    std::vector<Player> players = {
        {1, "Alice", Mark::X},
        {2, "Bob",   Mark::O},
    };
    Game game(3, players);

    // X plays the top row → X wins.
    game.makeMove(1, 0, 0);
    game.makeMove(2, 1, 0);
    game.makeMove(1, 0, 1);
    game.makeMove(2, 1, 1);
    game.makeMove(1, 0, 2);   // top row complete

    if (game.getStatus() == GameStatus::WIN)
        std::cout << "Winner: " << game.getWinner()->name << "\n";  // Alice

    // Illegal move now throws.
    try {
        game.makeMove(2, 2, 2);
    } catch (const std::exception& e) {
        std::cout << "Rejected: " << e.what() << "\n";  // game is already over
    }
    return 0;
}
```

> **Thinking habit:** a 15-line driver that hits the happy path *and* an error path is worth more than paragraphs of explanation.

---

## Step 7 — Talk through the follow-ups (don't necessarily code them all)

Show the seams are already there:

1. **More than 2 players.** Turn logic already round-robins. Only the win-counter needs upgrading from signed sums to **per-line per-player counts** (see Step 4 warning). One data-structure swap, interface unchanged.

2. **Pluggable win rule (Connect-Four "K in a row").** Extract a `WinningStrategy` interface and inject it into `Game`:

   ```cpp
   class WinningStrategy {
   public:
       virtual ~WinningStrategy() = default;
       // Return the winning mark if `move` completed a win, else Mark::EMPTY.
       virtual Mark checkWin(const Board& board, int row, int col, Mark mark) = 0;
   };
   ```

   The current `±N` logic becomes `FullLineStrategy`; Connect-Four becomes `KInARowStrategy`. `Game` depends on the interface, not the rule → **Strategy pattern**, exactly the "pattern focus" the problem named.

3. **Undo / replay.** Push each `(player, row, col)` onto a `std::vector<Move>` history. Undo = pop, clear the cell, and **subtract** the deltas from the counters (counters are reversible — another payoff of the incremental design). Replay = walk the list forward.

> **Thinking habit:** good LLD answers end by pointing at the extension points and naming the pattern that fits — it proves your abstractions weren't accidental.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** (N×N, O(1)) — they pick your data structures.
2. **Nouns → classes**, one responsibility each (`Player`, `Cell`, `Board`, `Game`).
3. **Interface first** — lock the contract.
4. **Leaves first** (`Player`, `Cell`), then the hard core (`Board` counters), then the orchestrator (`Game`).
5. **Incremental state** beats rescanning — signed sums for 2 players, count arrays for many.
6. **Validate → mutate → transition** in `makeMove`.
7. **Driver as proof**, then **name the seams** (Strategy, undo via reversible counters).

Follow that skeleton on any "design a game/board/state-machine" LLD and the code falls out almost mechanically.
