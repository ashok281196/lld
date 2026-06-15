# Parking Lot — LLD Problem Statement

**Difficulty:** Medium
**Language:** C++
**Pattern focus:** Strategy (slot allocation) + Factory + pricing strategy

> The single most-asked LLD question. Interviewers keep adding requirements to probe whether your abstractions hold.

---

## Context
Design a multi-floor parking lot system that issues tickets, allocates spots, and charges on exit.

## Functional Requirements
- The lot has **multiple floors**, each with **spots of several types**: Motorcycle, Compact (car), Large (truck/bus).
- On **entry**, a vehicle is allocated a compatible spot; the system issues a **ticket** (entry time, spot, vehicle).
- On **exit**, the ticket is closed, the **fee is computed** (time-based, possibly per vehicle type), payment is taken, and the spot is freed.
- Reject entry when **no compatible spot** is available (lot full for that type).
- Support **multiple entry/exit gates**.

## Non-Functional / Constraints
- **Spot-allocation strategy** must be pluggable (nearest-to-entrance, first-available, per-floor balancing).
- **Pricing strategy** must be pluggable (flat hourly, per-vehicle-type, day/night, free first 15 min).
- **Thread safety**: concurrent entries must never assign the same spot twice.

## Expected Public Interface
```cpp
enum class VehicleType { MOTORCYCLE, CAR, TRUCK };

class SpotAllocationStrategy {        // Strategy
public:
    virtual ParkingSpot* findSpot(VehicleType, const ParkingLot&) = 0;
    virtual ~SpotAllocationStrategy() = default;
};

class PricingStrategy {               // Strategy
public:
    virtual double calculate(const Ticket&) const = 0;
    virtual ~PricingStrategy() = default;
};

class ParkingLot {
public:
    Ticket parkVehicle(const Vehicle& v);          // throws if full
    Receipt unparkVehicle(const Ticket& t);        // computes fee, frees spot
    bool isFull(VehicleType) const;
};
```

## What the Interviewer Is Really Testing
- Clean modeling of `ParkingLot → Floor → ParkingSpot`, `Vehicle` hierarchy, `Ticket`.
- **Pluggable** allocation and pricing strategies — they will swap requirements mid-interview.
- The **concurrency** answer for spot assignment (lock per floor / atomic claim).

## Follow-Up Questions to Expect
1. EV spots with **charging**, handicapped-reserved spots.
2. **Reservation / pre-booking** ahead of arrival.
3. **Find-my-car** and real-time availability display per floor.
4. Lost-ticket handling and flat penalty.

## Your Task
1. Lock down assumptions + interface, then the entity model.
2. Implement one allocation strategy and one pricing strategy behind the interfaces.
3. State and implement the concurrency-safe spot claim.
