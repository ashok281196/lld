# Tic-Tac-Toe — LLD Problem Statement

**Difficulty:** Easy
**Language:** C++
**Pattern focus:** Strategy (pluggable win rule), clean board modeling

---

## Context
Design a 2-player Tic-Tac-Toe game playable on an N×N board (default 3×3).

## Functional Requirements
- Two players take alternating turns placing their mark (X / O) on an empty cell.
- The game detects a **win** (a full row, column, or either diagonal of the same mark) or a **draw** (board full, no winner).
- Reject invalid moves (occupied cell, out-of-bounds, wrong player's turn) gracefully — no crash, no silent corruption of state.
- Generalize cleanly to N×N — the win condition must **not** be hardcoded to 3.

## Non-Functional / Constraints
- Win-check should be efficient: do **not** rescan the entire board after every move. Maintain per-row, per-column, and per-diagonal counters and check in O(1) per move.
- Single-threaded is fine for the base version.

## Expected Public Interface
```cpp
enum class GameStatus { IN_PROGRESS, WIN, DRAW };

class Game {
public:
    Game(int boardSize, std::vector<Player> players);
    void makeMove(int playerId, int row, int col);   // throws/returns error on invalid move
    GameStatus getStatus() const;
    const Player* getWinner() const;                  // nullptr if no winner yet
};
```

## What the Interviewer Is Really Testing
- Clean separation of `Board`, `Cell`, `Player`, `Game`.
- The O(1) win-check via counters (this is the differentiator from a naive answer).
- Whether your abstractions survive the generalization to N×N.

## Follow-Up Questions to Expect
1. Support **more than 2 players** on a larger board.
2. Make the win rule **pluggable** (e.g. Connect-Four style "K marks in a row" via a `WinningStrategy` interface).
3. Add **undo / replay** of the move history.

## Your Task
1. Spend the first 2–3 minutes writing assumptions + the public interface above.
2. Implement entities and the O(1) win-check.
3. Then evolve toward at least one follow-up.
