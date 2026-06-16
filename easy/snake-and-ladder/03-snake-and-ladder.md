# Snake & Ladder — LLD Problem Statement

**Difficulty:** Easy
**Language:** C++
**Pattern focus:** Clean entity modeling + Strategy (pluggable dice)

---

## Context
Design the classic Snake & Ladder board game for N players on a 100-cell board.

## Functional Requirements
- **Configurable board**: snakes (head cell → tail cell) and ladders (bottom cell → top cell) placed at arbitrary positions.
- Players **roll a die** in turn, advance their token, and the position adjusts if they land on a **snake head** (slide down) or **ladder bottom** (climb up).
- The first player to land **exactly** on cell 100 wins. Decide and **state your overshoot rule** (e.g. a roll that would overshoot is forfeited).
- Support a **pluggable die**: single die, two dice, or a crooked/loaded die.

## Non-Functional / Constraints
- Almost no algorithm here — this is purely about **clean class boundaries** and abstracting the die behind an interface.

## Expected Public Interface
```cpp
class Dice {
public:
    virtual int roll() = 0;     // SixSidedDice, TwoDice, CrookedDice
    virtual ~Dice() = default;
};

struct Jump {  // models both snakes and ladders
    int start;
    int end;
};

class Game {
public:
    Game(int boardSize, std::vector<Jump> jumps, std::unique_ptr<Dice> dice);
    void addPlayer(const Player& p);
    Player startGame();          // runs the simulation loop, returns the winner
};
```

## What the Interviewer Is Really Testing
- Separation into `Board`, `Cell` / `Jump`, `Dice`, `Player`, `Game`.
- The `Dice` interface so behaviour swaps without touching `Game`.
- Whether snakes and ladders are unified as one `Jump` concept (start → end) rather than two duplicated classes.

## Follow-Up Questions to Expect
1. **Roll again on a 6** rule (and a cap of 3 consecutive sixes resets the turn).
2. Validate the board at construction: no two jumps share a start cell, no jump starts/ends on cell 1 or 100, and no jump cycles.
3. Multiple dice and configurable winning cell.

## Your Task
1. Write assumptions (board size, overshoot rule) and the interface first.
2. Model the entities; keep the simulation loop thin.
3. Add board validation as the follow-up.
