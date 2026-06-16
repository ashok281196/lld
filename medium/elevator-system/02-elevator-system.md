# Elevator System — LLD Problem Statement

**Difficulty:** Medium
**Language:** C++
**Pattern focus:** State pattern + pluggable scheduling/dispatch strategy

---

## Context
Design the control system for a building with multiple elevator cars serving many floors.

## Functional Requirements
- **External (hall) requests**: a button on a floor with a direction (UP / DOWN).
- **Internal (cabin) requests**: a destination-floor button inside a car.
- A central **dispatcher** assigns hall requests to the most suitable car.
- Each **car** has a state: `IDLE / MOVING_UP / MOVING_DOWN / DOORS_OPEN / MAINTENANCE`.
- A car services requests in a sensible order (e.g. SCAN/elevator algorithm — keep going one direction, then reverse), not strictly FIFO.

## Non-Functional / Constraints
- The **dispatch/scheduling strategy** must be pluggable (nearest-car, least-load, direction-aware SCAN).
- Car movement is driven by **state objects** (State pattern), not flag soup.
- Concurrency: many requests arrive simultaneously from different floors.

## Expected Public Interface
```cpp
enum class Direction { UP, DOWN, IDLE };

class DispatchStrategy {              // Strategy
public:
    virtual int selectCar(const HallRequest&, const std::vector<Elevator>&) = 0;
    virtual ~DispatchStrategy() = default;
};

class ElevatorController {
public:
    void requestElevator(int floor, Direction dir);   // external/hall call
    void step();                                       // advance the simulation one tick
};

class Elevator {
public:
    void addStop(int floor);                           // internal/cabin call
    void move();                                        // delegates to current state
    int  currentFloor() const;
    Direction direction() const;
};
```

## What the Interviewer Is Really Testing
- Separation of **hall request** vs **cabin request** and how each enters the system.
- A **State pattern** for the car and a **Strategy** for dispatch.
- The request-ordering logic (SCAN) — naive FIFO is a red flag at SDE-3.

## Follow-Up Questions to Expect
1. **Multiple cars** with load balancing and a shared dispatcher.
2. **Express / VIP** elevators and floor restrictions (e.g. service floors).
3. **Maintenance mode** taking a car out of rotation safely.
4. Capacity / weight limits and skipping full cars.

## Your Task
1. Define assumptions + interface, then `ElevatorController`, `Elevator`, request types.
2. Implement the car State pattern and one dispatch strategy.
3. Implement SCAN ordering, then add the multi-car follow-up.
