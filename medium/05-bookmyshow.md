# BookMyShow (Movie Ticket Booking) — LLD Problem Statement

**Difficulty:** Medium
**Language:** C++
**Pattern focus:** Seat-lock concurrency + Observer (notifications) + payment State

---

## Context
Design the booking subsystem for a movie-ticketing platform: browse shows, select seats, and book with payment.

## Functional Requirements
- A **City** has **Theatres**; each theatre has **Screens**; each screen runs **Shows** of **Movies** at given times.
- A show has a **seat map**; each seat has a status: `AVAILABLE / LOCKED / BOOKED`.
- A user can **search shows** by city + movie, **select seats**, and **book** them.
- Booking flow: **lock** selected seats → take **payment** → **confirm** (mark BOOKED) or **release** on timeout/failure.
- **Two users must never book the same seat** (the central challenge).

## Non-Functional / Constraints
- **Concurrency**: seat locking must be atomic; a lock has a **timeout** after which seats auto-release.
- **Observer** for notifications (booking confirmed, payment failed, seats released).
- **Payment as a small State machine**: `PENDING → SUCCESS / FAILED`, with the booking reacting accordingly.

## Expected Public Interface
```cpp
enum class SeatStatus { AVAILABLE, LOCKED, BOOKED };

class SeatLockProvider {              // the concurrency core
public:
    bool lockSeats(const Show& show, const std::vector<Seat>& seats,
                   const std::string& userId);          // atomic; false if any taken
    void unlockSeats(const Show& show, const std::vector<Seat>& seats,
                     const std::string& userId);
    bool validateLock(const Show&, const Seat&, const std::string& userId) const;
};

class BookingService {
public:
    std::vector<Show> searchShows(const std::string& city, const std::string& movie);
    Booking createBooking(const std::string& userId, const Show& show,
                          const std::vector<Seat>& seats);   // locks seats
    Booking confirmBooking(const Booking& booking, const Payment& payment);
};
```

## What the Interviewer Is Really Testing
- The **atomic seat-lock with timeout** — this is the make-or-break detail.
- A clean entity hierarchy (City → Theatre → Screen → Show → Seat).
- **Observer** wired for notifications; **payment state** handled explicitly.

## Follow-Up Questions to Expect
1. **Distributed locking** if the service is horizontally scaled (discuss, even if you implement in-memory).
2. **Dynamic / tiered pricing** (recliner vs regular, weekend surge) via a pricing strategy.
3. **Waitlist** and auto-allocation when seats release.
4. Idempotent booking under client retries.

## Your Task
1. Assumptions + interface, then the entity hierarchy.
2. Implement the in-memory `SeatLockProvider` with timeout + thread safety.
3. Wire Observer notifications and the payment state transitions.
