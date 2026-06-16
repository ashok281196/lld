# Chess — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes **polymorphic move rules + the Command pattern** feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"Move rules must be **polymorphic per piece** — no giant switch on piece type."* → This is **the** thing being tested. A `switch (pieceType)` answer is a fail. Each piece is a subclass that owns its own `canMove`.
2. *"Moves modeled as **Command** objects (carry enough state to undo)."* → Undo/redo isn't a bolt-on; it forces every move to be a reversible object that captures what it destroyed (a captured piece, a moved-flag, an en-passant victim).
3. *"Detect check, checkmate, stalemate."* → The subtle rule: a move is legal only if it **doesn't leave your own king in check**. That means *try the move, test, maybe roll back* — which is exactly why reversible Command objects pay off twice.
4. *"Board/move-validation must be testable in isolation."* → `Board` and `Piece` know geometry and movement; they must not depend on `Game`, turns, or clocks.
5. Follow-ups name the seams: PGN history/replay, chess clock, a **bot behind a `MoveStrategy`** (Strategy), networking/serialization.

> **Thinking habit:** when the prompt says "polymorphic, not switch" *and* "commands that undo," it is naming two patterns at once. Build both; don't fight either.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *board, cell, square, piece, king/queen/rook/bishop/knight/pawn, color, move, capture, turn, player, check, game.*

| Class | Owns | Why it exists |
|-------|------|---------------|
| `Color` (enum) | WHITE / BLACK | whose piece / whose turn |
| `Cell` | row, col, and the `Piece*` sitting on it | one square of board state |
| `Piece` (abstract) | its color, moved-flag, and `canMove` rule | **polymorphic** movement; one subclass per piece type |
| `King`/`Queen`/`Rook`/`Bishop`/`Knight`/`Pawn` | that piece's geometry | the actual move rules |
| `Board` | the 8×8 grid + piece lookup (incl. king location) | geometry, occupancy, "is square attacked?" |
| `Move` (abstract) | from/to + captured state | **Command**: `execute` / `undo` |
| `NormalMove` / `CastleMove` / … | the specifics of one kind of move | reversible application of a move |
| `Game` | players, turn, status, undo/redo stacks | orchestrates: validate → apply → switch turn |

> **Thinking habit:** spot the two pattern roles in the noun list — a **class hierarchy** (pieces) and a **command hierarchy** (moves). The rest of the classes are context and leaves.

---

## Step 2 — Pin the public interface (the contract)

Given to us — lock it before internals:

```cpp
enum class Color { WHITE, BLACK };
enum class GameStatus { IN_PROGRESS, CHECK, CHECKMATE, STALEMATE, DRAW };
enum class MoveResult { OK, ILLEGAL, NOT_YOUR_TURN, GAME_OVER };

class Piece {                          // polymorphic move rules
public:
    virtual bool canMove(const Board&, const Cell& from, const Cell& to) const = 0;
    virtual ~Piece() = default;
protected:
    Color color_;
};

class Move {                           // Command — supports undo
public:
    virtual void execute(Board&) = 0;
    virtual void undo(Board&) = 0;
    virtual ~Move() = default;
};

class Game {
public:
    MoveResult playMove(const Cell& from, const Cell& to);  // validates, applies, switches turn
    void undo();
    void redo();
    GameStatus status() const;
};
```

Two decisions baked in here:
- `Piece::canMove` answers a **pure geometry question** ("could this piece legally slide from→to on *this* board?"). It deliberately does **not** know about turns or check — that keeps it testable in isolation.
- `Move::execute/undo` make every move a **reversible Command**. `Game` keeps two stacks of these and never mutates the board by hand.

> **Thinking habit:** decide *what each method is allowed to know*. `canMove` knowing about check would couple geometry to game state — and the problem explicitly forbids that coupling.

---

## Step 3 — Model the leaves: `Color`, `Cell`, `Board`

Bottom-up: things with the fewest dependencies first.

```cpp
struct Cell {
    int row = 0, col = 0;          // 0..7
    Piece* piece = nullptr;        // non-owning view of what stands here (Board owns the piece)
    bool isEmpty() const { return piece == nullptr; }
};
```

`Board` hides the 8×8 grid and answers geometry questions. It also caches each king's square so check-tests stay cheap. It **owns** the pieces (via `unique_ptr`) but hands out raw `Piece*` for "who's standing where."

```cpp
class Board {
public:
    Board() { grid_.assign(8, std::vector<Cell>(8)); init_coords(); }

    bool inBounds(int r, int c) const { return r >= 0 && r < 8 && c >= 0 && c < 8; }
    Cell&       at(int r, int c)       { return grid_[r][c]; }
    const Cell& at(int r, int c) const { return grid_[r][c]; }

    // Raw board mutation used by Move::execute/undo. No rule-checking here —
    // legality is decided before a Move is ever built.
    void setPiece(int r, int c, Piece* p) {
        grid_[r][c].piece = p;
        if (p && p->isKing()) kingCell_[colorIdx(p->color())] = &grid_[r][c];
    }
    Piece* pieceAt(int r, int c) const { return grid_[r][c].piece; }
    const Cell& kingCell(Color c) const { return *kingCell_[colorIdx(c)]; }

    // Is `target` attacked by ANY piece of `byColor`? (pure geometry sweep)
    bool isAttacked(const Cell& target, Color byColor) const {
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                Piece* p = grid_[r][c].piece;
                if (p && p->color() == byColor &&
                    p->canMove(*this, grid_[r][c], target))
                    return true;
            }
        return false;
    }

private:
    static int colorIdx(Color c) { return c == Color::WHITE ? 0 : 1; }
    void init_coords() {
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) { grid_[r][c].row = r; grid_[r][c].col = c; }
    }
    std::vector<std::vector<Cell>> grid_;
    Cell* kingCell_[2] = {nullptr, nullptr};
    // (a vector<unique_ptr<Piece>> member holds ownership; omitted for brevity)
};
```

> **Thinking habit:** the leaf that everything leans on (`Board`) should expose *questions* (`inBounds`, `isAttacked`, `pieceAt`) and *raw mutation* (`setPiece`), but **no rules**. Rules live one layer up. That's what "testable in isolation" buys you.

---

## Step 4 — The key insight: polymorphic piece rules

This is the heart of the problem. Spend real thought here.

**Naive idea:** one `canMove` with `switch (piece.type) { case ROOK: … }`. Every new piece edits that switch — the exact anti-pattern the prompt bans.

**The polymorphism trick.** Make `Piece` abstract with one pure-virtual `canMove`. Each concrete piece answers *only its own geometry*. Adding a piece = adding a subclass; no existing code changes (open/closed).

```cpp
class Piece {
public:
    explicit Piece(Color c) : color_(c) {}
    virtual ~Piece() = default;

    // Pure geometry: ignores turns and check. "Could I slide from->to on this board?"
    virtual bool canMove(const Board&, const Cell& from, const Cell& to) const = 0;
    virtual bool isKing() const { return false; }
    virtual char symbol() const = 0;   // 'R','N','B','Q','K','P' — handy for PGN/debug

    Color color() const { return color_; }
    bool  hasMoved() const { return moved_; }
    void  setMoved(bool m) { moved_ = m; }

protected:
    // Shared helper: can't land on your own piece; sliders need a clear path.
    bool destOk(const Cell& to) const {
        return to.isEmpty() || to.piece->color() != color_;
    }
    Color color_;
    bool  moved_ = false;   // needed for castling & pawn's first 2-step
};
```

Two representative subclasses — a "slider" and a "jumper":

```cpp
class Rook : public Piece {
public:
    using Piece::Piece;
    char symbol() const override { return 'R'; }
    bool canMove(const Board& b, const Cell& from, const Cell& to) const override {
        if (from.row != to.row && from.col != to.col) return false;  // must be straight line
        if (!destOk(to)) return false;
        return pathClear(b, from, to);                               // no jumping over pieces
    }
};

class Knight : public Piece {
public:
    using Piece::Piece;
    char symbol() const override { return 'N'; }
    bool canMove(const Board&, const Cell& from, const Cell& to) const override {
        int dr = std::abs(from.row - to.row), dc = std::abs(from.col - to.col);
        return ((dr == 2 && dc == 1) || (dr == 1 && dc == 2)) && destOk(to);  // L-shape, jumps freely
    }
};
```

`Bishop`/`Queen`/`King` follow the same shape (diagonals, both, one step in any direction). `pathClear` is a shared free function that walks the straight/diagonal between two cells and rejects if any intermediate square is occupied — sliders use it, jumpers don't.

> ⚠️ **`canMove` is intentionally "pseudo-legal."** It answers geometry only. It does *not* check "would this leave my king in check?" — that's `Game`'s job (Step 6), because answering it requires trying the move on the board. Keeping the split here is what lets `Piece` be unit-tested with a bare `Board` and no `Game`.

> **Thinking habit:** push behaviour *down* into the type that owns it. When "what can X do?" varies by X's type, that's a virtual method, never a switch.

---

## Step 5 — The Command: `Move` carries enough state to undo

A move must be undoable, so it has to remember everything it destroyed. A plain capture, for instance, must stash the captured piece *and* the squares involved.

```cpp
class Move {
public:
    virtual ~Move() = default;
    virtual void execute(Board&) = 0;
    virtual void undo(Board&) = 0;
};

class NormalMove : public Move {
public:
    NormalMove(Cell from, Cell to) : from_(from), to_(to) {}

    void execute(Board& b) override {
        moved_      = b.pieceAt(from_.row, from_.col);
        captured_   = b.pieceAt(to_.row,   to_.col);   // may be nullptr — remembered for undo
        wasFirstMove_ = !moved_->hasMoved();

        b.setPiece(to_.row,   to_.col,   moved_);       // move the piece
        b.setPiece(from_.row, from_.col, nullptr);      // vacate origin
        moved_->setMoved(true);
    }

    void undo(Board& b) override {
        b.setPiece(from_.row, from_.col, moved_);       // put it back
        b.setPiece(to_.row,   to_.col,   captured_);    // restore whatever was captured (or nullptr)
        if (wasFirstMove_) moved_->setMoved(false);
    }

private:
    Cell from_, to_;
    Piece* moved_    = nullptr;
    Piece* captured_ = nullptr;   // THE crucial bit of state for undo
    bool   wasFirstMove_ = false; // restore moved-flag exactly (matters for castling/pawn)
};
```

The captured piece is not deleted on `execute` — `Board` still owns it; the square merely stops pointing at it. `undo` re-points the square. That's why undo is lossless.

> **Thinking habit:** a Command must capture *everything its execution overwrote*. The captured piece and the `hasMoved` flag are easy to forget — and forgetting them is exactly where undo bugs hide.

---

## Step 6 — Orchestrate with `Game`: validate (incl. self-check), apply, switch turn

`Game` is the rule engine. `playMove` runs a fixed pipeline. Note steps 3–5: we *try* the move with the Command, test our own king, and **roll back if it's illegal** — reusing the very same `undo` that powers user-facing undo.

```cpp
class Game {
public:
    MoveResult playMove(const Cell& from, const Cell& to) {
        if (status_ == GameStatus::CHECKMATE || status_ == GameStatus::STALEMATE)
            return MoveResult::GAME_OVER;

        Piece* p = board_.pieceAt(from.row, from.col);
        if (!p || p->color() != turn_) return MoveResult::NOT_YOUR_TURN;

        // 1. pseudo-legal geometry check (delegated to the piece — polymorphism)
        if (!p->canMove(board_, board_.at(from.row, from.col), board_.at(to.row, to.col)))
            return MoveResult::ILLEGAL;

        // 2. build the Command and try it
        auto move = std::make_unique<NormalMove>(from, to);
        move->execute(board_);

        // 3. self-check rule: did I just expose my own king?
        if (board_.isAttacked(board_.kingCell(turn_), opponent(turn_))) {
            move->undo(board_);                      // illegal — roll back, same code path as undo()
            return MoveResult::ILLEGAL;
        }

        // 4. legal: commit to history, clear the redo stack
        undoStack_.push_back(std::move(move));
        redoStack_.clear();

        // 5. switch turn and recompute status
        turn_ = opponent(turn_);
        recomputeStatus();
        return MoveResult::OK;
    }

    void undo() {
        if (undoStack_.empty()) return;
        auto m = std::move(undoStack_.back()); undoStack_.pop_back();
        m->undo(board_);
        redoStack_.push_back(std::move(m));
        turn_ = opponent(turn_);
        recomputeStatus();
    }

    void redo() {
        if (redoStack_.empty()) return;
        auto m = std::move(redoStack_.back()); redoStack_.pop_back();
        m->execute(board_);
        undoStack_.push_back(std::move(m));
        turn_ = opponent(turn_);
        recomputeStatus();
    }

    GameStatus status() const { return status_; }

private:
    static Color opponent(Color c) { return c == Color::WHITE ? Color::BLACK : Color::WHITE; }

    // Checkmate = in check AND no legal move escapes it; stalemate = not in check AND no legal move.
    void recomputeStatus() {
        bool inCheck = board_.isAttacked(board_.kingCell(turn_), opponent(turn_));
        bool anyLegal = hasAnyLegalMove(turn_);          // tries every pseudo-legal move w/ self-check
        if (inCheck && !anyLegal)       status_ = GameStatus::CHECKMATE;
        else if (!inCheck && !anyLegal) status_ = GameStatus::STALEMATE;
        else if (inCheck)               status_ = GameStatus::CHECK;
        else                            status_ = GameStatus::IN_PROGRESS;
    }
    bool hasAnyLegalMove(Color side);  // brute-force: every from→to, execute, test king, undo

    Board board_;
    Color turn_ = Color::WHITE;        // White moves first
    GameStatus status_ = GameStatus::IN_PROGRESS;
    std::vector<std::unique_ptr<Move>> undoStack_, redoStack_;
};
```

Three design wins to call out in an interview:
- **Self-check validation reuses `Move::undo`.** "Try → test → roll back" is the same machinery as user undo. One mechanism, two payoffs.
- **`recomputeStatus` derives check/mate/stalemate from two facts** — *am I in check?* and *do I have any legal move?* No special-casing per piece.
- **Status transitions live in one place.** Nobody outside `Game` sets `CHECKMATE`, so state can't drift.

> **Thinking habit:** the "would this leave my king in check?" rule is just *apply-test-rollback*. If your Move objects are honestly reversible, this rule is nearly free — and so is undo/redo.

---

## Step 7 — A special move as proof of extensibility: castling

Special moves are where naive designs collapse into `if (isCastle) …` scattered everywhere. With the Command pattern, a special move is **just another `Move` subclass** that moves two pieces and restores both on undo. The core pipeline in Step 6 doesn't change.

```cpp
class CastleMove : public Move {
public:
    CastleMove(Cell kingFrom, Cell kingTo, Cell rookFrom, Cell rookTo)
        : kf_(kingFrom), kt_(kingTo), rf_(rookFrom), rt_(rookTo) {}

    void execute(Board& b) override {
        king_ = b.pieceAt(kf_.row, kf_.col);
        rook_ = b.pieceAt(rf_.row, rf_.col);
        b.setPiece(kt_.row, kt_.col, king_); b.setPiece(kf_.row, kf_.col, nullptr);
        b.setPiece(rt_.row, rt_.col, rook_); b.setPiece(rf_.row, rf_.col, nullptr);
        king_->setMoved(true); rook_->setMoved(true);
    }
    void undo(Board& b) override {
        b.setPiece(kf_.row, kf_.col, king_); b.setPiece(kt_.row, kt_.col, nullptr);
        b.setPiece(rf_.row, rf_.col, rook_); b.setPiece(rt_.row, rt_.col, nullptr);
        king_->setMoved(false); rook_->setMoved(false);   // castling requires neither had moved
    }
private:
    Cell kf_, kt_, rf_, rt_;
    Piece* king_ = nullptr; Piece* rook_ = nullptr;
};
```

`Game` recognises a castling intent (king moving two squares) and, after checking the three castling pre-conditions — *neither piece has moved, the squares between are empty, and the king is not in/through/into check* — builds a `CastleMove` instead of a `NormalMove`. **En passant** and **promotion** are the same trick: each is one more `Move` subclass capturing its own undo state (the en-passant victim's square; the promoted-from pawn).

> **Thinking habit:** when a "special case" threatens to sprinkle `if`s through your core, ask "can this be a new subclass of an existing abstraction?" If yes, the special case becomes ordinary.

---

## Step 8 — Prove it with a tiny driver

Always show a `main` that exercises a legal move, an illegal (self-check) rejection, and an undo. It doubles as your test.

```cpp
#include <iostream>

int main() {
    Game game;                       // standard 8x8 starting position, White to move

    // A legal opening pawn push.
    auto r1 = game.playMove({6, 4}, {4, 4});   // e2 -> e4
    std::cout << "e4: " << (r1 == MoveResult::OK ? "OK" : "rejected") << "\n";

    // It's Black's turn now; trying to move a White piece is rejected.
    auto r2 = game.playMove({6, 3}, {4, 3});   // White d-pawn, but not White's turn
    std::cout << "out-of-turn: "
              << (r2 == MoveResult::NOT_YOUR_TURN ? "rejected" : "OK") << "\n";

    // Undo the opening push — board returns to the start, turn flips back to White.
    game.undo();
    auto r3 = game.playMove({6, 4}, {4, 4});   // legal again after undo
    std::cout << "replay after undo: " << (r3 == MoveResult::OK ? "OK" : "rejected") << "\n";

    std::cout << "status code: " << static_cast<int>(game.status()) << "\n";
    return 0;
}
```

> **Thinking habit:** a short driver that hits a legal move, an out-of-turn/illegal rejection, *and* an undo proves the three pillars — polymorphic geometry, turn rules, and reversible commands — in one screen.

---

## Step 9 — Talk through the follow-ups (don't necessarily code them all)

Show the seams are already there:

1. **Move history / PGN export and replay.** The `undoStack_` *is* the history. Give each `Move` a `notation()` method (it already knows from/to and the piece `symbol()`); walking the stack emits PGN. Replay = clear the board and `execute` the list forward. The Command objects make this nearly free.

2. **Timers (chess clock).** Orthogonal to move legality. Add a `Clock` per color; `playMove` debits the mover's clock and a flag-fall sets `status_ = DRAW`/loss. The `Move`/`Piece` hierarchies are untouched — timing is a `Game`-level concern.

3. **Bot opponent behind a `MoveStrategy` interface.** This is the named Strategy plug point:

   ```cpp
   class MoveStrategy {
   public:
       virtual ~MoveStrategy() = default;
       // Pick a move for `side` given the current game/board.
       virtual std::pair<Cell, Cell> chooseMove(const Game&, Color side) = 0;
   };
   ```

   `RandomStrategy` and `MinimaxStrategy` implement it; `Game` (or a driver) depends on the interface, not the algorithm. Random → minimax is a one-class swap → **Strategy pattern**, exactly the "pattern focus" the problem named.

4. **Networked two-client play + serialization.** Serialize board state (piece placements + turn + castling/en-passant rights) to FEN, or stream the `Move` list. Because moves are self-contained Commands, sending "the move that happened" is smaller and replay-safe versus shipping the whole board.

> **Thinking habit:** good LLD answers end by pointing at the extension points and naming the pattern that fits — Strategy for the bot, Command for history/network. It proves your abstractions weren't accidental.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — "polymorphic, not switch" + "commands that undo" literally name the two patterns.
2. **Nouns → classes**, spotting the two hierarchies: pieces (polymorphism) and moves (Command).
3. **Interface first** — and decide *what each method may know*; `canMove` is pure geometry, not check.
4. **Leaves first** (`Color`, `Cell`, `Board` with `isAttacked`), then the piece hierarchy, then moves, then `Game`.
5. **Polymorphic geometry** — each `Piece` owns its `canMove`; sliders share `pathClear`, jumpers don't.
6. **Reversible Commands** — a `Move` stashes the captured piece + moved-flags so `undo` is lossless.
7. **Validate via apply-test-rollback** — self-check reuses `Move::undo`; same machinery as user undo/redo.
8. **Special moves = new `Move` subclasses** (castling, en passant, promotion), never `if`s in the core.
9. **Follow-ups = a new strategy / a new orthogonal concern** (Strategy bot, clock, FEN), never a rewrite.

Follow that skeleton on any "rules engine with reversible actions" LLD (board games, text editor with undo, transactional workflows) and the polymorphism + Command design falls out almost mechanically.
