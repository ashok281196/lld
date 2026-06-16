# BookMyShow (Movie Ticket Booking) — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **concurrency-safe seat lock** feel inevitable rather than bolted on.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"Two users must never book the same seat."* → This is **the** thing being tested. Everything else (entities, payment, notifications) is scaffolding around one atomic operation: claiming a set of seats. A solution where two threads can both succeed on the last seat is a fail.
2. *"Seat locking must be atomic; a lock has a **timeout** after which seats auto-release."* → Locking isn't a boolean flag. It's an *all-or-nothing* claim over multiple seats, owned by a user, that **expires**. That phrasing screams: one mutex-guarded provider + a per-lock expiry timestamp.
3. *"Observer for notifications."* → Booking events fan out to interested parties (SMS, email, waitlist). Don't hardcode the side-effects; publish events.
4. *"Payment as a small State machine: `PENDING → SUCCESS / FAILED`."* → The booking reacts to a payment *outcome*, not a payment *method*. Model the outcome explicitly so confirm/release logic has one clean switch.
5. Follow-ups name the seams: distributed locking, tiered pricing (Strategy), waitlist, idempotent retries.

> **Thinking habit:** find the one sentence the whole problem pivots on ("never book the same seat"). Design that core airtight first; the rest is plumbing around it.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *city, theatre, screen, show, movie, seat, seat map, lock, booking, payment, user, notification.*

The entities form a clean containment hierarchy (**City → Theatre → Screen → Show → Seat**); the *behaviour* lives in three services.

| Class | Owns | Why it exists |
|-------|------|---------------|
| `Movie` | id, title | what's playing |
| `Seat` | id, row/col, type, price tier | the unit being booked |
| `Show` | movie, screen, start time, seat map | a specific screening you book against |
| `Screen` / `Theatre` / `City` | their children | the search/containment hierarchy |
| `SeatLock` | seats, userId, expiry timestamp | one timed, user-owned claim |
| `SeatLockProvider` | all active locks + the **mutex** | the **concurrency core** — atomic claim/release |
| `Payment` | id, amount, status (PENDING/SUCCESS/FAILED) | the payment outcome state |
| `Booking` | user, show, seats, status, payment | the transaction record |
| `BookingService` | lock provider, observers, repositories | orchestrates lock → pay → confirm/release |
| `BookingObserver` (interface) | `onEvent(...)` | pluggable notification targets |

> **Thinking habit:** separate the **data hierarchy** (City…Seat — dumb structs) from the **behaviour** (`SeatLockProvider`, `BookingService`). The hard logic always lives in a couple of services, not in the entities.

---

## Step 2 — Pin the public interface (the contract)

The statement hands us the shape. Lock it before internals — it forces the hard decision (what "atomic lock" returns) to the front.

```cpp
enum class SeatStatus { AVAILABLE, LOCKED, BOOKED };

class SeatLockProvider {                 // the concurrency core
public:
    // Atomic: locks ALL seats or NONE. false if any is already taken.
    bool lockSeats(const Show& show, const std::vector<Seat>& seats,
                   const std::string& userId);
    void unlockSeats(const Show& show, const std::vector<Seat>& seats,
                     const std::string& userId);
    bool validateLock(const Show& show, const Seat& seat,
                      const std::string& userId) const;
};

class BookingService {
public:
    std::vector<Show> searchShows(const std::string& city, const std::string& movie);
    Booking createBooking(const std::string& userId, const Show& show,
                          const std::vector<Seat>& seats);    // locks seats
    Booking confirmBooking(const Booking& booking, const Payment& payment);
};
```

Decisions baked in here:
- **`lockSeats` is all-or-nothing.** It returns `bool`, and on failure it must leave the world untouched — no partial locks. That single guarantee is what prevents double-booking.
- **`createBooking` locks but does not confirm.** Locking and paying are separate phases; a lock is a *reservation with a deadline*, not a sale.
- **`validateLock` is the gate before confirming** — payment success means nothing if your lock already expired and someone else grabbed the seat.

> **Thinking habit:** the return type of the critical method (`bool lockSeats`) encodes the whole correctness contract. Decide its semantics — atomic, no partial effects — before you write a single field.

---

## Step 3 — Model the leaves: entities + `SeatLock`

Bottom-up: dependency-free types first. The hierarchy entities are almost pure data.

```cpp
struct Movie {
    std::string id;
    std::string title;
};

enum class SeatType { REGULAR, PREMIUM, RECLINER };

struct Seat {
    std::string id;        // e.g. "A12" — unique within a show's seat map
    int row = 0, col = 0;
    SeatType type = SeatType::REGULAR;
    int price = 0;         // base price; tiering refined in the follow-up
};

struct Show {
    std::string id;
    Movie movie;
    std::string screenId;
    std::string city;
    long long startTime = 0;     // epoch seconds
    std::vector<Seat> seatMap;
};
```

The interesting leaf is `SeatLock` — it carries the *timeout* the problem demanded. A lock is "these seats, held by this user, valid until this instant."

```cpp
#include <chrono>

class SeatLock {
public:
    SeatLock(std::vector<std::string> seatIds, std::string userId,
             std::chrono::seconds ttl)
        : seatIds_(std::move(seatIds)), userId_(std::move(userId)),
          expiry_(std::chrono::steady_clock::now() + ttl) {}

    bool isExpired() const { return std::chrono::steady_clock::now() > expiry_; }
    const std::string& userId() const { return userId_; }
    const std::vector<std::string>& seatIds() const { return seatIds_; }

private:
    std::vector<std::string> seatIds_;
    std::string userId_;
    std::chrono::steady_clock::time_point expiry_;
};
```

> **Thinking habit:** the moment a spec says "timeout / TTL / expires," store an **absolute expiry instant**, not a duration or a countdown. `now() > expiry` is the simplest, race-free expiry check.

---

## Step 4 — The key insight: an atomic, timed, multi-seat lock

This is the heart of the problem. Spend real thought here.

**The trap.** A naive design marks each seat `LOCKED` one at a time. Two threads booking seats `{A1, A2}` and `{A2, A3}` can interleave: thread 1 locks `A1`, thread 2 locks `A2` and `A3`, thread 1 tries `A2` and fails — but it already locked `A1`, so it must roll back. Worse, without a lock around the whole check-then-set, both threads can read "A2 available" before either writes. **That's the double-booking bug.**

**The fix — three rules:**

1. **One owner of all locks.** `SeatLockProvider` holds the single source of truth: a map `showId → (seatId → SeatLock)`. Nobody mutates seat-claim state except through it.
2. **Guard check-then-set with a mutex.** The "are all these seats free? then claim them" must be one critical section. No window between checking and claiming.
3. **All-or-nothing.** Inside the lock, *first* verify every requested seat is free (treating expired locks as free), *then* claim them all. If any check fails, claim nothing and return `false`.

A locked seat is "free" again the instant its lock expires — so the timeout is enforced lazily, right inside the availability check. No background thread required (though we'll mention one in the follow-ups).

```cpp
#include <mutex>
#include <unordered_map>

class SeatLockProvider {
public:
    explicit SeatLockProvider(std::chrono::seconds ttl) : ttl_(ttl) {}

    bool lockSeats(const Show& show, const std::vector<Seat>& seats,
                   const std::string& userId) {
        std::lock_guard<std::mutex> guard(mutex_);          // ONE critical section
        auto& showLocks = locks_[show.id];

        // Phase 1: verify EVERY seat is free (or held by an expired lock).
        for (const Seat& s : seats) {
            auto it = showLocks.find(s.id);
            if (it != showLocks.end() && !it->second.isExpired())
                return false;                               // taken -> claim nothing
        }

        // Phase 2: claim them all (we still hold the mutex -> atomic).
        for (const Seat& s : seats)
            showLocks.insert_or_assign(
                s.id, SeatLock({s.id}, userId, ttl_));
        return true;
    }

    void unlockSeats(const Show& show, const std::vector<Seat>& seats,
                     const std::string& userId) {
        std::lock_guard<std::mutex> guard(mutex_);
        auto& showLocks = locks_[show.id];
        for (const Seat& s : seats) {
            auto it = showLocks.find(s.id);
            if (it != showLocks.end() && it->second.userId() == userId)
                showLocks.erase(it);                        // only the owner may unlock
        }
    }

    bool validateLock(const Show& show, const Seat& seat,
                      const std::string& userId) const {
        std::lock_guard<std::mutex> guard(mutex_);
        auto showIt = locks_.find(show.id);
        if (showIt == locks_.end()) return false;
        auto it = showIt->second.find(seat.id);
        return it != showIt->second.end()
            && !it->second.isExpired()
            && it->second.userId() == userId;               // still mine, still valid
    }

private:
    mutable std::mutex mutex_;                              // mutable: const validateLock locks too
    std::chrono::seconds ttl_;
    std::unordered_map<std::string,
        std::unordered_map<std::string, SeatLock>> locks_;  // showId -> seatId -> lock
};
```

Why it's correct: the entire check-then-claim runs under one `lock_guard`, so no two threads can both pass Phase 1 for the same seat. Expired locks are transparently treated as free, so the timeout needs no separate sweeper to be *correct* — only to reclaim memory.

> **Thinking habit:** "no double-booking" = **atomic check-then-set under a single lock**. Verify-all-then-claim-all inside one critical section. If you mutate before you've verified the whole set, you've created a partial-failure rollback problem — avoid it by design.

---

## Step 5 — Model payment as a tiny state, and the `Booking` record

The problem says payment is a small state machine `PENDING → SUCCESS / FAILED`. We don't need a full State-pattern class hierarchy here — three enum values plus the booking's reaction to them is the right altitude.

```cpp
enum class PaymentStatus { PENDING, SUCCESS, FAILED };

struct Payment {
    std::string id;
    int amount = 0;
    PaymentStatus status = PaymentStatus::PENDING;
};

enum class BookingStatus { CREATED, CONFIRMED, CANCELLED };

struct Booking {
    std::string id;
    std::string userId;
    Show show;
    std::vector<Seat> seats;
    BookingStatus status = BookingStatus::CREATED;
};
```

The booking's lifecycle mirrors the payment's: a `SUCCESS` confirms (seats → `BOOKED`), a `FAILED` (or expiry) cancels and releases the lock. That mapping is the whole "payment state" requirement.

> **Thinking habit:** match the *weight* of your modeling to the requirement. A three-outcome payment doesn't need polymorphic State classes — an enum + a switch in `confirmBooking` is honest and readable. Reserve the heavy State pattern for genuinely behaviour-rich modes (like the vending machine).

---

## Step 6 — Wire Observer for notifications

The Observer requirement: booking events (confirmed, payment failed, seats released) fan out to subscribers without the service knowing who they are.

```cpp
enum class BookingEvent { CONFIRMED, PAYMENT_FAILED, SEATS_RELEASED };

class BookingObserver {
public:
    virtual ~BookingObserver() = default;
    virtual void onEvent(BookingEvent event, const Booking& booking) = 0;
};

// A concrete subscriber.
class EmailNotifier : public BookingObserver {
public:
    void onEvent(BookingEvent event, const Booking& booking) override {
        std::cout << "[email] " << booking.userId
                  << " event=" << static_cast<int>(event)
                  << " booking=" << booking.id << "\n";
    }
};
```

The service keeps a list of observers and publishes to all of them on every state change:

```cpp
void notify(BookingEvent event, const Booking& booking) {
    for (BookingObserver* obs : observers_)
        obs->onEvent(event, booking);
}
```

> **Thinking habit:** Observer earns its keep when the *list of reactions grows independently of the action*. Email today, SMS + waitlist-trigger tomorrow — the service publishes one event and never learns who's listening.

---

## Step 7 — Orchestrate with `BookingService`: lock → pay → confirm/release

The service is the conductor. Its two methods follow the **validate → mutate → transition** rhythm, with the lock provider doing the dangerous part.

`createBooking`: lock the seats (atomic) → on success record a `CREATED` booking; on failure throw. `confirmBooking`: re-validate the lock is still ours → branch on payment outcome → confirm or release → publish the event.

```cpp
class BookingService {
public:
    BookingService(SeatLockProvider& lockProvider,
                   std::vector<BookingObserver*> observers)
        : lockProvider_(lockProvider), observers_(std::move(observers)) {}

    std::vector<Show> searchShows(const std::string& city,
                                  const std::string& movie) {
        std::vector<Show> out;
        for (const Show& s : shows_)
            if (s.city == city && s.movie.title == movie)
                out.push_back(s);
        return out;
    }

    Booking createBooking(const std::string& userId, const Show& show,
                          const std::vector<Seat>& seats) {
        if (!lockProvider_.lockSeats(show, seats, userId))
            throw std::runtime_error("seats unavailable");   // someone else holds them

        Booking b;
        b.id     = nextBookingId();
        b.userId = userId;
        b.show   = show;
        b.seats  = seats;
        b.status = BookingStatus::CREATED;                   // locked, awaiting payment
        return b;
    }

    Booking confirmBooking(const Booking& booking, const Payment& payment) {
        Booking b = booking;

        // Gate: the lock must still be ours for EVERY seat (timeout may have fired).
        for (const Seat& s : b.seats) {
            if (!lockProvider_.validateLock(b.show, s, b.userId)) {
                b.status = BookingStatus::CANCELLED;
                notify(BookingEvent::SEATS_RELEASED, b);     // lock expired under us
                return b;
            }
        }

        if (payment.status == PaymentStatus::SUCCESS) {
            b.status = BookingStatus::CONFIRMED;             // seats become BOOKED
            // (the lock has served its purpose; booking is now permanent)
            notify(BookingEvent::CONFIRMED, b);
        } else {                                             // FAILED or still PENDING
            lockProvider_.unlockSeats(b.show, b.seats, b.userId);
            b.status = BookingStatus::CANCELLED;
            notify(BookingEvent::PAYMENT_FAILED, b);
        }
        return b;
    }

    void addShow(const Show& s) { shows_.push_back(s); }

private:
    void notify(BookingEvent event, const Booking& booking) {
        for (BookingObserver* obs : observers_)
            obs->onEvent(event, booking);
    }
    std::string nextBookingId() { return "BK" + std::to_string(++counter_); }

    SeatLockProvider& lockProvider_;
    std::vector<BookingObserver*> observers_;
    std::vector<Show> shows_;
    int counter_ = 0;
};
```

Two design wins to call out in an interview:
- **The lock provider is the only thing that needs to be thread-safe.** The service is a thin coordinator; correctness is concentrated in one class, which is exactly where you want it.
- **`confirmBooking` re-validates before committing.** Payment success is necessary but not sufficient — the lock must still be valid. That guards the timeout race ("I paid, but my hold expired") cleanly.

> **Thinking habit:** concentrate the tricky invariant in one class (`SeatLockProvider`). Then the orchestrator can be boring — and boring orchestrators have fewer bugs.

---

## Step 8 — Prove it with a driver (including the race)

A driver must show the happy path *and* the double-booking attempt being rejected. Spinning up two threads on the same seat is the demonstration that sells the whole design.

```cpp
#include <thread>
#include <atomic>

int main() {
    SeatLockProvider lockProvider(std::chrono::seconds(5));   // 5s hold
    EmailNotifier email;
    BookingService service(lockProvider, {&email});

    Show show;
    show.id = "S1"; show.city = "NYC";
    show.movie = {"M1", "Inception"};
    show.seatMap = { {"A1"}, {"A2"}, {"A3"} };
    service.addShow(show);

    std::vector<Seat> wanted = { {"A1"}, {"A2"} };

    // Two users race for the SAME seats. Exactly one must win the lock.
    std::atomic<int> wins{0};
    auto attempt = [&](const std::string& user) {
        try { service.createBooking(user, show, wanted); ++wins; }
        catch (const std::exception&) { /* lost the race, expected */ }
    };
    std::thread t1(attempt, "alice");
    std::thread t2(attempt, "bob");
    t1.join(); t2.join();

    std::cout << "winners: " << wins.load() << "\n";          // exactly 1

    // Confirm the winner with a successful payment.
    Booking b = service.createBooking("carol", show, { {"A3"} });
    Payment ok{"P1", 250, PaymentStatus::SUCCESS};
    Booking done = service.confirmBooking(b, ok);
    std::cout << "carol booking confirmed="
              << (done.status == BookingStatus::CONFIRMED) << "\n";

    // A failed payment releases the seats and fires PAYMENT_FAILED.
    Booking b2 = service.createBooking("dave", show, { {"A1"} }); // A1 free again? only if carol's lock differs
    Payment bad{"P2", 250, PaymentStatus::FAILED};
    service.confirmBooking(b2, bad);
    return 0;
}
```

The key assertion is `winners == 1`: no matter how the two threads interleave, the mutex serializes their check-then-claim, so exactly one acquires `{A1, A2}`.

> **Thinking habit:** for any concurrency answer, your driver should *attempt the race*, not just the happy path. Two threads on one seat, asserting one winner — that's the proof an interviewer wants.

---

## Step 9 — Talk through the follow-ups (don't necessarily code them all)

Show the seams are already there:

1. **Distributed locking (horizontally scaled service).** The in-memory `std::mutex` only protects one process. Swap `SeatLockProvider`'s internals for a **shared store with atomic claims** — Redis `SET seat:<show>:<id> <user> NX EX <ttl>` is the textbook move: `NX` gives all-or-nothing, `EX` gives the timeout for free. The *interface* (`lockSeats` / `validateLock`) is unchanged — only the implementation behind it moves from a hashmap to Redis. Naming this "the interface stays, the backend swaps" is the answer.

2. **Dynamic / tiered pricing.** Extract a `PricingStrategy` interface (`int priceFor(const Show&, const Seat&)`) and inject it into the service. Recliner-vs-regular, weekend surge, demand-based — each is a strategy implementation. **Strategy pattern**, orthogonal to the lock logic.

3. **Waitlist + auto-allocation on release.** Already an Observer payoff: when seats release (`SEATS_RELEASED`), a `WaitlistObserver` reacts by offering them to the next queued user. The service publishes the event and stays oblivious — exactly why we used Observer.

4. **Idempotent booking under retries.** A client retry must not create a second booking or a second lock. Have the client pass an **idempotency key**; the service keeps a `key → Booking` map and returns the existing booking if the key repeats. The lock provider's `lockSeats` is also naturally idempotent for the *same* user (re-locking your own seats is a no-op via `insert_or_assign` with the same owner).

5. **Reclaiming expired locks.** Correctness needs no sweeper (expired = free in the check). But to free memory, a background thread can periodically erase expired entries under the same mutex.

> **Thinking habit:** good LLD answers end by pointing at the extension points and naming the pattern that fits — distributed lock (same interface, new backend), Strategy for pricing, Observer for waitlist. It proves your abstractions weren't accidental.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — "never book the same seat" + "atomic lock with timeout" picks your core data structure (a mutex-guarded lock map).
2. **Separate data from behaviour** — City…Seat are dumb structs; the logic lives in `SeatLockProvider` and `BookingService`.
3. **Interface first** — `bool lockSeats(...)` with all-or-nothing semantics encodes the whole correctness contract.
4. **Leaves first** (entities, then `SeatLock` with an **absolute expiry instant**).
5. **The core: atomic check-then-set under one mutex** — verify all seats free (expired = free), then claim all. No partial effects.
6. **Match modeling weight to the requirement** — payment is an enum + a switch, not a heavyweight State hierarchy.
7. **Observer** so notifications fan out without the service knowing the listeners.
8. **Orchestrate** lock → pay → re-validate → confirm/release; concentrate the invariant in one class.
9. **Driver proves the race** (two threads, one winner), then **name the seams** (distributed lock, Strategy pricing, Observer waitlist, idempotency key).

Follow that skeleton on any "concurrent reservation" LLD (BookMyShow, airline seats, parking lot, hotel rooms) and the design falls out almost mechanically — guard the one claim operation, and let everything else stay boring.
