# Snake & Ladder — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **clean entity split** and the **Strategy pattern for the die** feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the constraints first — they decide the design. And note what's *absent*: there's barely any algorithm here, so the whole grade is on your **class boundaries** and your **abstractions**.

1. *"Support a **pluggable die**: single die, two dice, or a crooked/loaded die."* → This is **the** thing being tested. The die behind an interface is a textbook **Strategy pattern**. A hardcoded `rand() % 6 + 1` inside `Game` is a fail.
2. *"Snakes (head → tail) and ladders (bottom → top)…"* → Both are just a *teleport from one cell to another*. Unify them as **one `Jump` concept** (`start → end`). Two separate `Snake` / `Ladder` classes is duplicated code with no behavioural difference — interviewers watch for this.
3. *"The first player to land **exactly** on cell 100 wins… **state your overshoot rule**."* → There's an ambiguity on purpose. You must **declare an assumption** out loud (we forfeit a roll that overshoots) rather than guess silently.
4. *"Almost no algorithm here — purely about clean class boundaries."* → Keep the simulation loop **thin**. The win is in the modeling, not cleverness.
5. Follow-ups name the seams: roll-again-on-6, board validation, configurable winning cell.

> **Thinking habit:** when the prompt says "pluggable X," it's naming a **Strategy**. When two nouns differ only in direction (snake down / ladder up), they're **one** concept — collapse them.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *board, cell, snake, ladder, jump, die, player, token, position, game, turn, winner.*

Collapse the synonyms (snake + ladder → `Jump`; token + position → an `int` on `Player`) and group the rest by responsibility:

| Class | Owns | Why it exists |
|-------|------|---------------|
| `Jump` (struct) | `start`, `end` cells | unifies snake and ladder as one "teleport" |
| `Dice` (interface) | the `roll()` contract | abstracts *how many pips* so behaviour swaps freely |
| `SixSidedDice` / `TwoDice` / `CrookedDice` | their own RNG/bias | the concrete roll behaviours |
| `Board` | size + `start → end` jump map | knows geometry; answers "where does this cell send me?" |
| `Player` | id, name, current position | identity + token location |
| `Game` | board, players, dice, turn order | orchestrates the simulation, declares the winner |

> **Thinking habit:** before listing classes, *merge synonyms*. Fewer, sharper concepts beat a class per noun. `Jump` instead of `Snake`+`Ladder` is the move that earns points here.

---

## Step 2 — Pin the public interface (the contract)

The statement hands us the shape. Lock it before internals — it forces you to decide what the outside world sees.

```cpp
class Dice {
public:
    virtual int roll() = 0;          // SixSidedDice, TwoDice, CrookedDice
    virtual ~Dice() = default;
};

struct Jump {                        // models both snakes and ladders
    int start;
    int end;
};

class Game {
public:
    Game(int boardSize, std::vector<Jump> jumps, std::unique_ptr<Dice> dice);
    void   addPlayer(const Player& p);
    Player startGame();              // runs the simulation loop, returns the winner
};
```

Decisions baked in here:
- **`Dice` is an abstract base with one pure-virtual `roll()`.** `Game` will depend on `Dice&`/`Dice*`, never on a concrete die → swapping behaviour never touches `Game`.
- **`Game` takes `std::unique_ptr<Dice>`** — it *owns* the injected die. Ownership is explicit and the die is polymorphic, so we pass it by pointer, not value (no slicing).
- **`startGame()` returns the winning `Player`.** A single value out; the loop runs to completion internally.

> **Thinking habit:** inject the abstraction (`unique_ptr<Dice>`), own it explicitly, and have the orchestrator depend only on the interface. That one constructor signature *is* the Strategy pattern wiring.

---

## Step 3 — Model the leaves: `Jump`, `Player`, and the `Dice` family

Bottom-up: things with no dependencies first.

`Jump` is a plain pair of cells. The *sign* of the move (down for a snake, up for a ladder) is implicit in `start` vs `end` — no boolean, no subclass needed.

```cpp
struct Jump {
    int start;   // snake head OR ladder bottom
    int end;     // snake tail OR ladder top  (end < start ⇒ snake, end > start ⇒ ladder)
};
```

`Player` is identity plus a token position. Position starts at 0 ("off the board"); the first roll moves the player on.

```cpp
struct Player {
    int id;
    std::string name;
    int position = 0;
};
```

The `Dice` family is where Strategy lives. One interface, several behaviours:

```cpp
#include <random>

class SixSidedDice : public Dice {
public:
    int roll() override { return dist_(rng_); }
private:
    std::mt19937 rng_{std::random_device{}()};
    std::uniform_int_distribution<int> dist_{1, 6};
};

class TwoDice : public Dice {                 // sum of two independent dice → 2..12
public:
    int roll() override { return dist_(rng_) + dist_(rng_); }
private:
    std::mt19937 rng_{std::random_device{}()};
    std::uniform_int_distribution<int> dist_{1, 6};
};

class CrookedDice : public Dice {             // loaded: always even, biased high
public:
    int roll() override { return dist_(rng_) * 2; }   // 2,4,6
private:
    std::mt19937 rng_{std::random_device{}()};
    std::uniform_int_distribution<int> dist_{1, 3};
};
```

Each die hides its own RNG and bias. `Game` will never know which one it holds — it just calls `roll()`.

> **Thinking habit:** build leaf types with zero dependencies first, and let polymorphism, not flags, encode variants. The day a `LoadedDice` appears, it's a *new class*, not an `if` inside `Game`.

---

## Step 4 — The key insight: unify snake+ladder behind `Board`, hide the lookup

This is the modeling crux. A snake and a ladder do the *same thing* — land on a special cell, get teleported elsewhere. So `Board` stores **one map** `start → end` and answers a single question: *"if a token lands on cell `p`, where does it actually end up?"*

```cpp
#include <unordered_map>

class Board {
public:
    Board(int size, const std::vector<Jump>& jumps) : size_(size) {
        for (const Jump& j : jumps)
            jumps_[j.start] = j.end;          // snakes and ladders share one table
    }

    int size() const { return size_; }

    // Resolve a landing cell: follow a jump if one starts here, else stay put.
    int finalPosition(int landed) const {
        auto it = jumps_.find(landed);
        return (it != jumps_.end()) ? it->second : landed;
    }

private:
    int size_;
    std::unordered_map<int, int> jumps_;       // start cell -> end cell
};
```

Why this is the right shape:
- **No `Snake`/`Ladder` duplication.** Direction is data (`end < start` vs `end > start`), not type. One `finalPosition` covers both.
- **`Board` owns geometry only.** It doesn't know about players, turns, or dice. Ask it where a cell sends you; that's the whole API.
- **O(1) lookup.** Trivial, but it keeps the simulation loop clean — landing resolution is one map probe.

> ⚠️ A subtle real-board rule: should a jump's *destination* itself be a jump start (a ladder dropping you onto a snake head)? Most rulebooks say **no chaining** — you take exactly one jump. Our single `finalPosition` call enforces that naturally (it resolves once). If you wanted chaining, you'd loop until `finalPosition(p) == p`. Say which you chose.

> **Thinking habit:** when two entities differ only by *data* (direction), store them in one structure keyed by what they have in common (the trigger cell). The type system shouldn't carry information the data already encodes.

---

## Step 5 — Orchestrate with `Game`: a thin simulation loop

`Game` wires everything and runs turns. The constraint said "keep the loop thin," so each turn is just: **roll → advance with overshoot rule → resolve jump → check win → next player.**

State our **assumptions** explicitly (this is graded):
- Board is **100** cells by default; players start at position **0**.
- **Overshoot rule:** a roll that would take you *past* the final cell is **forfeited** — you don't move, and play passes on. You must land **exactly** on 100.
- Players take turns **round-robin**, in the order they were added.

```cpp
#include <stdexcept>

class Game {
public:
    Game(int boardSize, std::vector<Jump> jumps, std::unique_ptr<Dice> dice)
        : board_(boardSize, jumps), dice_(std::move(dice)) {
        if (!dice_) throw std::invalid_argument("dice must not be null");
    }

    void addPlayer(const Player& p) { players_.push_back(p); }

    Player startGame() {
        if (players_.size() < 2)
            throw std::invalid_argument("need at least 2 players");

        int turn = 0;
        while (true) {
            Player& current = players_[turn];

            int roll   = dice_->roll();                 // Strategy in action
            int target = current.position + roll;

            if (target <= board_.size()) {              // overshoot rule: skip if it would pass the end
                current.position = board_.finalPosition(target);   // resolve snake/ladder
                if (current.position == board_.size())
                    return current;                     // landed exactly on the final cell → winner
            }
            // else: forfeited move, stay put

            turn = (turn + 1) % players_.size();         // round-robin
        }
    }

private:
    Board board_;
    std::vector<Player> players_;
    std::unique_ptr<Dice> dice_;
};
```

Two design wins to call out in the interview:
- **`Game` depends only on `Dice` (the interface).** It calls `dice_->roll()` and never asks which die it is. Swapping `TwoDice` for `CrookedDice` changes nothing here.
- **The loop is genuinely thin** — roll, bound-check, resolve, win-check, advance. All the "what does this cell do" logic lives in `Board`; all the "how many pips" logic lives in `Dice`. `Game` only sequences them.

> **Thinking habit:** the orchestrator's job is *sequencing*, not *deciding*. If your game loop is full of branching detail, that detail belongs in the entities it's calling.

---

## Step 6 — Prove it with a tiny driver

Always show a `main` that builds a board, injects a die, and runs to a winner. It doubles as your test and demonstrates the Strategy injection.

```cpp
#include <iostream>

int main() {
    std::vector<Jump> jumps = {
        {2, 23}, {8, 12}, {32, 51},   // ladders (end > start)
        {97, 28}, {62, 18}, {88, 24}, // snakes  (end < start)
    };

    Game game(100, jumps, std::make_unique<SixSidedDice>());  // swap die freely here
    game.addPlayer({1, "Alice"});
    game.addPlayer({2, "Bob"});

    Player winner = game.startGame();
    std::cout << "Winner: " << winner.name << "\n";

    // Strategy swap is a one-line change, Game untouched:
    Game loaded(100, jumps, std::make_unique<CrookedDice>());
    loaded.addPlayer({1, "Alice"});
    loaded.addPlayer({2, "Bob"});
    std::cout << "Loaded-die winner: " << loaded.startGame().name << "\n";
    return 0;
}
```

> **Thinking habit:** a driver that runs the *same* `Game` with two different `Dice` implementations is the cleanest possible proof that your Strategy seam works.

---

## Step 7 — Talk through the follow-ups (don't necessarily code them all)

Show the seams are already there.

1. **Roll again on a 6 (cap of 3 consecutive sixes resets the turn).** This is a turn-policy tweak, isolated to the loop. Track a per-turn `sixCount`; only advance `turn` when the roll wasn't a 6 (or when `sixCount` hits 3 → forfeit the whole turn and pass on). `Board` and `Dice` are untouched — the rule lives where turns are sequenced.

   ```cpp
   int sixes = 0;
   while (true) {
       int roll = dice_->roll();
       if (roll == 6 && ++sixes == 3) {       // third six: bust, lose the turn
           sixes = 0;
           turn = (turn + 1) % players_.size();
           continue;
       }
       /* ...advance + resolve + win-check as before... */
       if (roll != 6) { sixes = 0; turn = (turn + 1) % players_.size(); }
       // roll == 6 (and not the 3rd) → same player rolls again, don't advance turn
   }
   ```

2. **Validate the board at construction.** Push checks into `Board`'s constructor so an illegal board can never exist: no two jumps share a `start`; no jump touches cell 1 or the final cell; no jump is a self-loop (`start == end`); and (if you forbid chaining) no `end` is also a `start`.

   ```cpp
   Board(int size, const std::vector<Jump>& jumps) : size_(size) {
       for (const Jump& j : jumps) {
           if (j.start <= 1 || j.start >= size || j.end <= 1 || j.end >= size)
               throw std::invalid_argument("jump endpoints must be interior cells");
           if (j.start == j.end)
               throw std::invalid_argument("jump cannot be a self-loop");
           if (!jumps_.emplace(j.start, j.end).second)   // emplace fails if start already keyed
               throw std::invalid_argument("two jumps share a start cell");
       }
   }
   ```

   "No jump cycles" matters only if you allow chaining; with single-resolution `finalPosition`, a cycle can't trap you, but rejecting `end`-that-is-a-`start` keeps boards sane.

3. **Multiple dice / configurable winning cell.** Multiple dice is *already done* — that's exactly what `TwoDice` is, behind the same `Dice` interface. Configurable winning cell is just `board_.size()` (or a separate `winningCell_` member); the loop already compares against it, no special-casing. Two requirements, zero new branches.

> **Thinking habit:** good LLD answers end by mapping each follow-up to *where* it lands: turn rules → the loop, structural rules → the constructor, behaviour variants → a new Strategy subclass. Naming the location proves the boundaries were drawn right.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — "pluggable die" names a **Strategy**; "snakes and ladders" is *one* `Jump` concept, not two classes.
2. **Merge synonyms first** (`Snake`+`Ladder` → `Jump`; token+position → an `int`), then list classes.
3. **Interface first** — `Dice::roll()` plus the `Game` constructor that *injects* `unique_ptr<Dice>` is the whole Strategy wiring.
4. **Leaves first** (`Jump`, `Player`, the `Dice` family); encode variants with polymorphism, not flags.
5. **`Board` owns geometry**, unifies jumps in one `start → end` map, answers `finalPosition` in O(1).
6. **Keep the loop thin** — roll → overshoot-check → resolve → win-check → advance; `Game` *sequences*, it doesn't *decide*.
7. **State your assumptions** (board size, overshoot rule, no chaining) out loud.
8. **Follow-ups land predictably** — turn rules in the loop, structural rules in the constructor, new behaviour as a new `Dice` subclass.

Follow that skeleton on any "configurable board + pluggable behaviour" LLD (Ludo, Monopoly movement, dice/card simulators) and the entity split plus the Strategy seam fall out almost mechanically.
