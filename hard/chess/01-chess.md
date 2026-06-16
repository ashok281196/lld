# Chess — LLD Problem Statement

**Difficulty:** Hard
**Language:** C++
**Pattern focus:** Heavy polymorphism (piece move rules) + Command (undo/redo) + Strategy

---

## Context
Design a 2-player chess game engine: legal move generation, turn management, check/checkmate detection, and undo/redo.

## Functional Requirements
- An **8×8 board** with the six piece types (King, Queen, Rook, Bishop, Knight, Pawn), each with its **own movement rules**.
- Enforce **turn order** (White, then Black) and **legal moves** only.
- Detect **check**, **checkmate**, and **stalemate**.
- Support special moves: **castling**, **en passant**, **pawn promotion**.
- Support **undo / redo** of moves.

## Non-Functional / Constraints
- Move rules must be **polymorphic per piece** — no giant switch on piece type.
- Moves modeled as **Command** objects (carry enough state to undo) → enables undo/redo and move history.
- The board/move-validation must be testable in isolation.

## Expected Public Interface
```cpp
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
    GameStatus status() const;         // IN_PROGRESS / CHECK / CHECKMATE / STALEMATE / DRAW
};
```

## What the Interviewer Is Really Testing
- **Polymorphic piece rules** done cleanly (each `Piece` subclass owns `canMove`).
- **Command pattern** for moves enabling robust undo/redo and history.
- Correct **check/checkmate** logic — "would this move leave my own king in check?" validation.
- Handling the **special-case moves** without polluting the core.

## Follow-Up Questions to Expect
1. **Move history / PGN export** and replay.
2. **Timers** (chess clock) and time-out loss.
3. A **bot opponent** behind a `MoveStrategy` interface (random → minimax) — Strategy plug point.
4. **Networked** two-client play and serialization of board state.

## Your Task
1. Assumptions + interface, then the `Piece` hierarchy and `Board`.
2. Implement move validation including self-check detection.
3. Add Command-based undo/redo, then one special move (castling) as proof of extensibility.
