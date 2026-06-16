# Ride-Sharing (Uber / Ola) — LLD Problem Statement

**Difficulty:** Hard
**Language:** C++
**Pattern focus:** State machines (rider & driver & trip) + matching + Strategy (pricing/matching) + Observer

---

## Context
Design the core of a ride-hailing service: a rider requests a ride, the system matches a nearby driver, and the trip runs through its lifecycle with fare computation.

## Functional Requirements
- **Riders** request a ride from a pickup to a drop location, choosing a **ride type** (Mini / Sedan / SUV).
- **Drivers** go online/offline, publish their **location**, and accept/reject ride offers.
- The system **matches** a request to a suitable nearby available driver.
- **Trip lifecycle**: `REQUESTED → DRIVER_ASSIGNED → ARRIVED → IN_PROGRESS → COMPLETED / CANCELLED`.
- Compute **fare** on completion (base + distance + time, with surge).
- Notify rider and driver on each state change.

## Non-Functional / Constraints
- **Matching strategy** pluggable (nearest driver, highest-rated, least-idle).
- **Pricing strategy** pluggable (normal vs **surge** based on demand/supply).
- Driver and Trip modeled as **State machines**.
- **Observer** for rider/driver notifications.
- Concurrency: one driver must not be assigned to two trips; matching must be atomic.

## Expected Public Interface
```cpp
enum class TripStatus { REQUESTED, DRIVER_ASSIGNED, ARRIVED, IN_PROGRESS, COMPLETED, CANCELLED };

class MatchingStrategy {              // Strategy
public:
    virtual Driver* match(const RideRequest&, const std::vector<Driver>& available) = 0;
    virtual ~MatchingStrategy() = default;
};

class PricingStrategy {               // Strategy (normal / surge)
public:
    virtual double fare(const Trip&) const = 0;
    virtual ~PricingStrategy() = default;
};

class RideService {
public:
    Trip requestRide(const std::string& riderId, Location pickup, Location drop, RideType type);
    void acceptRide(const std::string& driverId, const std::string& tripId);
    void startTrip(const std::string& tripId);
    Receipt endTrip(const std::string& tripId);   // computes fare
    void cancelTrip(const std::string& tripId, const std::string& byActor);
};
```

## What the Interviewer Is Really Testing
- Multiple interacting **state machines** (driver availability vs trip lifecycle) kept consistent.
- **Atomic matching** so a driver isn't double-booked under concurrency.
- Pluggable **matching** and **surge pricing** strategies.
- **Observer** notifications threaded through state changes.

## Follow-Up Questions to Expect
1. **Geospatial matching** at scale (grid / quadtree / geohash for "nearby drivers").
2. **Cancellation policy** and fees (who cancelled, when).
3. **Pool / shared rides** (multiple riders per trip).
4. **Ratings**, driver payout, and ETA estimation.

## Your Task
1. Assumptions + interface, then `Rider`, `Driver`, `Trip`, `RideRequest`.
2. Implement the trip & driver state machines and an atomic matcher.
3. Add surge pricing and Observer notifications; discuss geospatial indexing as the scale follow-up.
