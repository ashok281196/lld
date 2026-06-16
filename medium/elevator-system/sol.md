# Elevator System — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **State pattern + pluggable dispatch Strategy** feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"Car movement is driven by **state objects** (State pattern), not flag soup."* → Each car is a little state machine: `IDLE / MOVING_UP / MOVING_DOWN / DOORS_OPEN / MAINTENANCE`. A `bool movingUp; bool doorsOpen;` answer is a fail.
2. *"The **dispatch/scheduling strategy** must be pluggable."* → The "which car serves this hall call" decision is a swappable **Strategy** (nearest-car, least-load, SCAN-aware). The controller depends on the interface, not a hardcoded rule.
3. *"Services requests in a sensible order (**SCAN**), not strictly FIFO."* → This is **the** algorithmic differentiator. Naive FIFO is the red flag the prompt warns about. The car keeps going one direction servicing stops in order, then reverses.
4. *"**Hall** request (floor + direction) vs **cabin** request (destination floor)"* → two distinct entry points: hall calls go through the dispatcher; cabin calls go straight to one car.
5. *"Concurrency: many requests arrive simultaneously."* → mention locking the request structures; don't let it warp the core design.

> **Thinking habit:** when the prompt names two patterns ("state objects" + "pluggable strategy"), it's handing you the architecture. One axis is *car mode* (State), the other is *which car* (Strategy). Keep the two axes orthogonal.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *building, elevator/car, floor, hall request, cabin request, direction, dispatcher/controller, state, strategy.*

| Class | Owns | Why it exists |
|-------|------|---------------|
| `Direction` (enum) | UP / DOWN / IDLE | the axis a car moves along |
| `HallRequest` | floor + desired direction | an external call waiting for *a* car |
| `ElevatorState` (interface) | `move()` behaviour per mode | one reaction-set per car mode |
| `IdleState` / `MovingUpState` / `MovingDownState` / `DoorsOpenState` / `MaintenanceState` | the per-mode movement + transition rules | the actual car behaviours |
| `Elevator` (the **Context**) | current floor, direction, current state, its **pending stops** (SCAN sets) | one physical car; delegates `move()` to its state |
| `DispatchStrategy` (interface) | `selectCar(...)` | the pluggable "which car?" decision |
| `NearestCarStrategy` | distance scoring | one concrete dispatch policy |
| `ElevatorController` | the cars + the strategy + the hall queue | routes hall calls, drives the simulation `step()` |

> **Thinking habit:** two patterns ⇒ two interfaces. Spot the **Context + State** triangle (`Elevator` + `ElevatorState`) *and* the **Strategy** seam (`ElevatorController` + `DispatchStrategy`) and the class list writes itself.

---

## Step 2 — Pin the public interface (the contract)

Given to us — lock it before internals:

```cpp
enum class Direction { UP, DOWN, IDLE };

class HallRequest {
public:
    int       floor;
    Direction dir;     // which way the caller wants to go
};

class DispatchStrategy {                          // Strategy
public:
    virtual ~DispatchStrategy() = default;
    virtual int selectCar(const HallRequest&,
                          const std::vector<Elevator>&) = 0;   // returns car index
};

class Elevator {
public:
    void      addStop(int floor);      // internal/cabin call
    void      move();                  // delegates to current state (one tick)
    int       currentFloor() const;
    Direction direction() const;
};

class ElevatorController {
public:
    void requestElevator(int floor, Direction dir);  // external/hall call
    void step();                                      // advance simulation one tick
};
```

Two realizations baked in here:
- `Elevator::move()` contains **no logic** — it forwards to the current state: `state_->move(*this);`. All movement behaviour lives in the states.
- `ElevatorController` never decides *which* car directly; it asks the injected `DispatchStrategy`. The policy is data, not code.

> **Thinking habit:** in State+Strategy designs, the Context's hot methods (`move`, `requestElevator`) should be thin: one delegates to a state, the other delegates to a strategy. If they grow `if`s, logic leaked out of the abstractions.

---

## Step 3 — Model the leaves: `Direction` and the requests

Bottom-up: things with no dependencies first.

```cpp
enum class Direction { UP, DOWN, IDLE };

// External call: someone on `floor` wants to travel `dir`. Not yet bound to a car.
struct HallRequest {
    int       floor;
    Direction dir;
};
```

A **cabin** request needs no class — it's just a destination floor handed to one specific car via `addStop(int)`. That asymmetry *is* the hall-vs-cabin distinction the interviewer is probing: a hall request must be *dispatched* (it has no car yet); a cabin request is already *inside* a car.

> **Thinking habit:** not every noun deserves a class. A cabin "request" is just an `int` stop on a car you already hold a handle to — model the difference, not the word.

---

## Step 4 — The key insight: SCAN ordering with two sorted stop-sets

This is the heart of the problem. Spend real thought here.

**Naive idea (the trap):** one FIFO queue of stops, serviced in arrival order. A car at floor 3 asked for `[10, 1, 9]` would go 3→10→1→9 — bouncing the whole shaft twice. The prompt explicitly calls this out as a red flag.

**The SCAN (elevator) algorithm.** A real elevator keeps moving in its current direction, serving every stop *on the way*, until none remain ahead — then it reverses. Implement that with **two ordered sets** of pending stops per car:

- `up_` — a `std::set<int>` of stops at or above us (served ascending).
- `down_` — a `std::set<int>` of stops at or below us (served descending, so iterate in reverse).

When a stop is added at floor `f` while the car is at `currentFloor_`:
- `f > currentFloor_` → goes in `up_`
- `f < currentFloor_` → goes in `down_`
- `f == currentFloor_` → open doors here now.

Servicing rule per tick:
- Moving **up**: take `*up_.begin()` as the target. When reached, erase it, open doors. When `up_` empties, flip to **down** (or `IDLE` if both empty).
- Moving **down**: take `*down_.rbegin()` (highest of the lower stops). Symmetric.

Because the sets are sorted, "the next stop in this direction" is `O(log n)` to find and the car never reverses while work remains ahead. That's SCAN.

```cpp
class StopBook {                 // the SCAN bookkeeping for one car
public:
    void add(int floor, int currentFloor) {
        if (floor > currentFloor)      up_.insert(floor);
        else if (floor < currentFloor) down_.insert(floor);
        // floor == currentFloor: caller handles "open here"
    }
    bool hasUp()   const { return !up_.empty(); }
    bool hasDown() const { return !down_.empty(); }
    bool empty()   const { return up_.empty() && down_.empty(); }

    int  nextUp()   const { return *up_.begin(); }     // lowest stop above
    int  nextDown() const { return *down_.rbegin(); }  // highest stop below
    void reachedUp(int f)   { up_.erase(f); }
    void reachedDown(int f) { down_.erase(f); }

private:
    std::set<int> up_;
    std::set<int> down_;
};
```

> **Thinking habit:** SCAN is "always keep the next stop *ahead of you* cheap to find." A sorted set per direction makes "next stop this way" `O(log n)` and bans the FIFO bounce for free. Reach for the data structure that makes the required ordering a side effect.

---

## Step 5 — The Context: `Elevator` holds data + helpers states drive

The car exposes *just enough* for its states to do their job: read/move floor, read/set direction, touch its `StopBook`, switch state. These helpers are the car's "internal API for its states."

```cpp
class ElevatorState;   // forward declaration — Elevator holds a state pointer

class Elevator {
public:
    Elevator(int id, int startFloor);   // starts IDLE (constructor in Step 6b)

    // ---- public API ----
    void addStop(int floor) {
        if (floor == floor_) { openDoorsHere(); return; }
        stops_.add(floor, floor_);
        wakeIfIdle();                 // an idle car must pick a direction
    }
    void      move() { state_->move(*this); }   // pure delegation — one tick
    int       currentFloor() const { return floor_; }
    Direction direction()    const { return dir_; }

    // ---- helpers the STATES use to drive the car ----
    void setState(ElevatorState* s) { state_ = s; }
    ElevatorState* idle();          // accessors to the state singletons
    ElevatorState* movingUp();
    ElevatorState* movingDown();
    ElevatorState* doorsOpen();
    ElevatorState* maintenance();

    StopBook& stops()        { return stops_; }
    int  floor() const       { return floor_; }
    void stepFloor(int delta){ floor_ += delta; }     // physically move one floor
    void setDirection(Direction d) { dir_ = d; }
    void openDoorsHere();           // erase stop at floor_, go to DoorsOpen
    void wakeIfIdle();              // if currently idle and stops exist, choose a direction

    int  id() const { return id_; }

private:
    int id_;
    int floor_;
    Direction dir_ = Direction::IDLE;
    StopBook stops_;
    std::unique_ptr<ElevatorState> idle_, up_, down_, doors_, maint_;  // car OWNS states
    ElevatorState* state_ = nullptr;                                    // current (view)
};
```

> **Thinking habit:** split methods into *public API* (`move`, `addStop`) and *state-driver helpers* (`stepFloor`, `setDirection`, `setState`). The helpers are why each state can stay tiny and data-free.

---

## Step 6 — Implement the car states (each defines its own `move`)

This is where "movement driven by state objects, not flag soup" lives. Every state answers `move(Elevator&)`; the difference is *how* each reacts and *where* it transitions.

```cpp
class ElevatorState {
public:
    virtual ~ElevatorState() = default;
    virtual void        move(Elevator&) = 0;
    virtual Direction   direction() const = 0;     // what this mode reports
    virtual std::string name() const = 0;
};

// ---------- Idle: no pending stops, sitting still ----------
class IdleState : public ElevatorState {
public:
    void move(Elevator&) override { /* nothing to do; stay put */ }
    Direction direction() const override { return Direction::IDLE; }
    std::string name() const override { return "IDLE"; }
};

// ---------- MovingUp: serve the lowest stop above, ascending ----------
class MovingUpState : public ElevatorState {
public:
    void move(Elevator& e) override {
        if (!e.stops().hasUp()) {                  // nothing left above
            if (e.stops().hasDown()) { e.setDirection(Direction::DOWN); e.setState(e.movingDown()); }
            else                     { e.setDirection(Direction::IDLE); e.setState(e.idle()); }
            return;
        }
        int target = e.stops().nextUp();
        if (e.floor() == target) { e.openDoorsHere(); return; }   // arrived -> doors
        e.stepFloor(+1);                                          // climb one floor
        if (e.floor() == target) e.openDoorsHere();
    }
    Direction direction() const override { return Direction::UP; }
    std::string name() const override { return "MOVING_UP"; }
};

// ---------- MovingDown: serve the highest stop below, descending ----------
class MovingDownState : public ElevatorState {
public:
    void move(Elevator& e) override {
        if (!e.stops().hasDown()) {
            if (e.stops().hasUp()) { e.setDirection(Direction::UP); e.setState(e.movingUp()); }
            else                   { e.setDirection(Direction::IDLE); e.setState(e.idle()); }
            return;
        }
        int target = e.stops().nextDown();
        if (e.floor() == target) { e.openDoorsHere(); return; }
        e.stepFloor(-1);
        if (e.floor() == target) e.openDoorsHere();
    }
    Direction direction() const override { return Direction::DOWN; }
    std::string name() const override { return "MOVING_DOWN"; }
};

// ---------- DoorsOpen: a one-tick stop; then resume direction ----------
class DoorsOpenState : public ElevatorState {
public:
    void move(Elevator& e) override {
        // doors were open this tick; close and resume whichever way still has work
        if (e.stops().hasUp() && e.direction() != Direction::DOWN) {
            e.setDirection(Direction::UP);   e.setState(e.movingUp());
        } else if (e.stops().hasDown()) {
            e.setDirection(Direction::DOWN); e.setState(e.movingDown());
        } else if (e.stops().hasUp()) {
            e.setDirection(Direction::UP);   e.setState(e.movingUp());
        } else {
            e.setDirection(Direction::IDLE); e.setState(e.idle());
        }
    }
    Direction direction() const override { return Direction::IDLE; } // physically stopped
    std::string name() const override { return "DOORS_OPEN"; }
};

// ---------- Maintenance: out of rotation; ignores movement ----------
class MaintenanceState : public ElevatorState {
public:
    void move(Elevator&) override { /* frozen until taken out of maintenance */ }
    Direction direction() const override { return Direction::IDLE; }
    std::string name() const override { return "MAINTENANCE"; }
};
```

The shared helpers tie it together:

```cpp
void Elevator::openDoorsHere() {
    stops_.reachedUp(floor_);     // erase the stop here from whichever set held it
    stops_.reachedDown(floor_);
    setState(doorsOpen());
}
void Elevator::wakeIfIdle() {
    if (state_ != idle_.get()) return;             // already moving / in doors
    if (stops_.hasUp())   { dir_ = Direction::UP;   setState(movingUp()); }
    else if (stops_.hasDown()) { dir_ = Direction::DOWN; setState(movingDown()); }
}
```

> **Thinking habit:** write each state as "what does `move` do here, and which state do I become?" Fill in every mode — `MAINTENANCE` doing nothing is a *real* answer, not a gap. No undefined modes = no flag soup.

---

## Step 6b — Wire the Context constructor

Now that the states exist, build them once and start `IDLE`:

```cpp
Elevator::Elevator(int id, int startFloor) : id_(id), floor_(startFloor) {
    idle_  = std::make_unique<IdleState>();
    up_    = std::make_unique<MovingUpState>();
    down_  = std::make_unique<MovingDownState>();
    doors_ = std::make_unique<DoorsOpenState>();
    maint_ = std::make_unique<MaintenanceState>();
    state_ = idle_.get();           // start IDLE
}
ElevatorState* Elevator::idle()        { return idle_.get(); }
ElevatorState* Elevator::movingUp()    { return up_.get(); }
ElevatorState* Elevator::movingDown()  { return down_.get(); }
ElevatorState* Elevator::doorsOpen()   { return doors_.get(); }
ElevatorState* Elevator::maintenance() { return maint_.get(); }
```

The car **owns** its five states via `unique_ptr`; `state_` is a non-owning view. Because the states hold no per-car data, one instance of each per car is plenty.

> **Thinking habit:** own long-lived objects in one place (`unique_ptr` members), pass around raw/non-owning pointers for "who's current." Ownership vs. usage — keep them separate.

---

## Step 7 — The Strategy: pluggable dispatch

The second axis. `ElevatorController` must answer "which car serves this hall call?" — and the prompt says that policy must be swappable. So it's an injected interface, never an `if` in the controller.

```cpp
class NearestCarStrategy : public DispatchStrategy {
public:
    int selectCar(const HallRequest& req, const std::vector<Elevator>& cars) override {
        int best = -1, bestDist = std::numeric_limits<int>::max();
        for (int i = 0; i < static_cast<int>(cars.size()); ++i) {
            const Elevator& c = cars[i];
            // prefer cars idle, or already moving toward the request in the same direction
            bool suitable = c.direction() == Direction::IDLE
                         || c.direction() == req.dir;
            if (!suitable) continue;
            int dist = std::abs(c.currentFloor() - req.floor);
            if (dist < bestDist) { bestDist = dist; best = i; }
        }
        if (best == -1) best = 0;   // fallback: everyone busy the wrong way -> car 0
        return best;
    }
};
```

Swapping in `LeastLoadStrategy` (fewest pending stops) or a stricter SCAN-aware policy changes **only** this object. The controller is untouched. That's the open/closed payoff the prompt is hunting for.

> **Thinking habit:** "must be pluggable" is the interviewer literally spelling out *Strategy*. Make the decision an injected interface so a new policy is a new class, never an edit to the controller.

---

## Step 8 — The controller: route hall calls, drive `step()`

`ElevatorController` wires the two axes together: it holds the cars and the strategy, accepts hall calls, and ticks the simulation.

```cpp
class ElevatorController {
public:
    ElevatorController(int numCars, std::unique_ptr<DispatchStrategy> strategy)
        : strategy_(std::move(strategy)) {
        for (int i = 0; i < numCars; ++i)
            cars_.emplace_back(i, /*startFloor=*/0);
    }

    // external/hall call: pick a car via the strategy, then push the stop onto it
    void requestElevator(int floor, Direction dir) {
        std::lock_guard<std::mutex> lock(mtx_);
        HallRequest req{floor, dir};
        int idx = strategy_->selectCar(req, cars_);
        cars_[idx].addStop(floor);          // assigned car will SCAN to it
    }

    // cabin call: passenger already inside car `carId`
    void pressFloor(int carId, int dest) {
        std::lock_guard<std::mutex> lock(mtx_);
        cars_[carId].addStop(dest);
    }

    void step() {                            // advance every car one tick
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& c : cars_) c.move();
    }

    const Elevator& car(int i) const { return cars_[i]; }

private:
    std::vector<Elevator> cars_;
    std::unique_ptr<DispatchStrategy> strategy_;
    std::mutex mtx_;                         // guards cars_ against concurrent requests
};
```

Note the concurrency answer is *small and contained*: one mutex around the shared `cars_` mutations. The State/Strategy design didn't bend to accommodate it.

> **Thinking habit:** keep cross-cutting concerns (locking) at the orchestrator boundary. If concurrency forces you to redesign the core abstractions, the abstractions were wrong.

---

## Step 9 — Prove it with a tiny driver

Show a `main` that exercises SCAN ordering (no FIFO bounce), a cabin call, and the tick loop. It doubles as your test.

```cpp
#include <iostream>

int main() {
    auto controller = ElevatorController(/*numCars=*/1,
                                         std::make_unique<NearestCarStrategy>());

    // Car at floor 0. Three stops arrive "out of order": 5, then 2, then 8 (all above).
    controller.requestElevator(5, Direction::UP);
    controller.pressFloor(0, 2);     // cabin request inside car 0
    controller.requestElevator(8, Direction::UP);

    // SCAN must serve them ascending: 2, 5, 8 — NOT 5, 2, 8 (FIFO).
    for (int tick = 0; tick < 12; ++tick) {
        controller.step();
        const Elevator& c = controller.car(0);
        std::cout << "tick " << tick << ": floor " << c.currentFloor() << "\n";
    }
    // Expect doors to open at 2, then 5, then 8, in that order.
    return 0;
}
```

> **Thinking habit:** design the driver to *disprove the wrong answer*. Asking for 5 then 2 and showing the car stops at 2 first is what proves you implemented SCAN, not FIFO.

---

## Step 10 — Talk through the follow-ups (don't necessarily code them all)

Show the seams are already there:

1. **Multiple cars + load balancing.** Already supported — the controller holds a `std::vector<Elevator>` and asks the strategy. Swap `NearestCarStrategy` for `LeastLoadStrategy` (score by `stops` count / estimated time). One new class, controller unchanged.

2. **Express / VIP elevators and floor restrictions.** Two clean options: (a) a `bool canServe(int floor)` predicate on `Elevator` that the strategy filters on, or (b) an express car as a strategy that only considers a subset of floors. Restrictions live in the *dispatch policy*, not the state machine.

3. **Maintenance mode taking a car out safely.** Already a first-class state. A car finishes its current `DoorsOpen` cycle, then `setState(maintenance())`; the dispatch strategy skips any car whose `direction()`/state reports `MAINTENANCE`. Re-enable = `setState(idle())`. No other code changes.

4. **Capacity / weight limits, skipping full cars.** Add `int load_` + `int capacity_` to `Elevator`; the strategy filters out full cars (`load_ >= capacity_`), and a full car ignores new hall assignments until someone exits. Again: a *strategy filter* + one car field — the State pattern is untouched.

> **Thinking habit:** good LLD answers end by mapping each follow-up to an existing seam — "new dispatch policy = new Strategy subclass," "new car mode = new State subclass." If every follow-up is a new subclass and never a new `switch`, your abstractions were right.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — "state objects" + "pluggable strategy" name *two* patterns on *two* axes (car mode vs. which car).
2. **Nouns → classes** across both axes: Context (`Elevator`), State interface, concrete states; Strategy interface, concrete policies; orchestrator (`ElevatorController`).
3. **Interface first** — `move()` delegates to a state; `requestElevator()` delegates to a strategy. Thin hot methods.
4. **Leaves first** (`Direction`, `HallRequest`); note a cabin call is just an `int` stop — model the hall-vs-cabin asymmetry, not the word.
5. **SCAN, not FIFO** — two sorted stop-sets make "next stop this direction" cheap and ban the bounce.
6. **Context holds data + driver-helpers**; states are **stateless** and own their **transitions**.
7. **Strategy injected** — dispatch policy is data; concurrency is one mutex at the controller boundary.
8. **Driver disproves FIFO**; **follow-ups = new subclasses** (new policy / new mode), never a new `switch`.

Follow that skeleton on any "fleet of machines with modes + a routing decision" LLD (ride dispatch, print-job scheduler, load balancer) and the State + Strategy combo falls out almost mechanically.
