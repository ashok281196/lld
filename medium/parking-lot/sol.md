# Parking Lot — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **Strategy pattern** (pluggable allocation + pricing) feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"Spot-allocation strategy **must be pluggable** (nearest-to-entrance, first-available, per-floor balancing)."* → This is **the** thing being tested. A hardcoded "scan floors for first empty spot" answer is a fail. Allocation is a **Strategy** injected into the lot.
2. *"Pricing strategy **must be pluggable** (flat hourly, per-vehicle-type, day/night, free first 15 min)."* → A second, independent **Strategy**. Fee math must not live inside `unparkVehicle`.
3. *"**Thread safety**: concurrent entries must never assign the same spot twice."* → The differentiator. The find-spot → claim-spot step must be **atomic**, or two cars race onto the same spot. Naive `find()` then `occupy()` with no lock is a fail.
4. *"Reject entry when **no compatible spot** is available."* → A `VehicleType` → spot-compatibility rule, and a clean rejection path (throw).
5. Follow-ups name the seams: EV/handicapped spots (more spot subtypes), reservations, find-my-car, lost-ticket penalty. We leave room, don't build them all.

> **Thinking habit:** when the prompt says a behaviour "must be pluggable," it is literally naming **Strategy**. Two pluggable axes (allocation, pricing) = two independent strategy interfaces.

---

## Step 1 — Find the nouns → these become your classes

Read the prose and circle the nouns: *lot, floor, spot, vehicle, type, ticket, receipt, gate, fee, allocation, pricing.*

Group them into responsibilities:

| Class | Owns | Why it exists |
|-------|------|---------------|
| `VehicleType` (enum) | MOTORCYCLE / CAR / TRUCK | the kind of vehicle, drives spot compatibility |
| `Vehicle` | license plate, type | who is parking |
| `ParkingSpot` | id, spot type, occupied flag, current vehicle | smallest unit of parkable space |
| `ParkingFloor` | a collection of spots | geometry + per-floor locking unit |
| `Ticket` | id, vehicle, spot, entry time | the open contract for one parking session |
| `Receipt` | ticket + computed fee | the closed, paid result on exit |
| `SpotAllocationStrategy` (interface) | "where should this vehicle go?" | pluggable placement policy |
| `PricingStrategy` (interface) | "what does this session cost?" | pluggable fee policy |
| `ParkingLot` (the orchestrator) | floors + the two strategies + open tickets | issues tickets, closes them, enforces thread safety |

> **Thinking habit:** Strategy pattern = one **client** (the lot) + one **strategy interface** per pluggable axis + N **concrete strategies**. Two "must be pluggable" requirements means two interfaces — don't fold them into one.

---

## Step 2 — Pin the public interface (the contract)

Given to us — lock it before internals:

```cpp
enum class VehicleType { MOTORCYCLE, CAR, TRUCK };

class SpotAllocationStrategy {        // Strategy #1
public:
    virtual ParkingSpot* findSpot(VehicleType, const ParkingLot&) = 0;
    virtual ~SpotAllocationStrategy() = default;
};

class PricingStrategy {               // Strategy #2
public:
    virtual double calculate(const Ticket&) const = 0;
    virtual ~PricingStrategy() = default;
};

class ParkingLot {
public:
    Ticket  parkVehicle(const Vehicle& v);     // throws if full
    Receipt unparkVehicle(const Ticket& t);    // computes fee, frees spot
    bool    isFull(VehicleType) const;
};
```

Decisions baked in here:
- **Strategies are injected**, not built inside `ParkingLot`. The lot depends on the *interfaces*, never on a concrete policy — that's the whole point.
- **No compatible spot → throw** (`std::runtime_error`). Clean, testable, no silent "parked nowhere."
- `parkVehicle` returns a `Ticket` by value (the open session); `unparkVehicle` returns a `Receipt` (the priced, closed session). The two value types make "open vs. closed" explicit.

> **Thinking habit:** the interface is a promise. The strategies appear *in the constructor*, not as `if`s inside the methods — that's how you prove the policy is pluggable.

---

## Step 3 — Model the leaves: `Vehicle`, `ParkingSpot`, `Ticket`, `Receipt`

Bottom-up: things with no dependencies first. A vehicle is just identity + type.

```cpp
struct Vehicle {
    std::string plate;
    VehicleType type;
};
```

`ParkingSpot` is the unit that gets claimed. Give it a *spot type* (which vehicle sizes it accepts) and an occupancy flag. The `tryClaim` / `release` methods below become the atomic primitive for concurrency in Step 5 — design them now.

```cpp
class ParkingSpot {
public:
    ParkingSpot(int id, VehicleType spotType) : id_(id), spotType_(spotType) {}

    int  id() const         { return id_; }
    VehicleType type() const { return spotType_; }
    bool isFree() const     { return !occupied_; }

    // A MOTORCYCLE fits a motorcycle spot; a CAR fits a compact spot, etc.
    // (Simple 1:1 here; relax to "small fits in large" if the prompt asks.)
    bool fits(VehicleType v) const { return v == spotType_; }

    // Atomic claim: returns true only if THIS call flipped free -> occupied.
    // The exchange is the concurrency primitive (see Step 5).
    bool tryClaim()  { bool was = occupied_; occupied_ = true; return !was; }
    void release()   { occupied_ = false; }

private:
    int         id_;
    VehicleType spotType_;
    bool        occupied_ = false;
};
```

`Ticket` is the open contract; `Receipt` is the closed one. Keep them plain.

```cpp
using Clock     = std::chrono::system_clock;
using TimePoint = std::chrono::time_point<Clock>;

struct Ticket {
    int          id;
    Vehicle      vehicle;
    ParkingSpot* spot;        // non-owning: the lot owns the spot
    TimePoint    entryTime;
};

struct Receipt {
    Ticket    ticket;
    TimePoint exitTime;
    double    fee;
};
```

> **Thinking habit:** build bottom-up, and design a leaf's *mutating* methods (`tryClaim`/`release`) with the hard requirement (atomic assignment) already in mind. The leaf is where concurrency is won or lost.

---

## Step 4 — The key insight: two independent Strategies, injected

This is the heart of the problem. Allocation and pricing are **orthogonal** policies — neither knows about the other — so each gets its own interface and its own concrete implementation.

```
        ParkingLot (client)
          │ holds  allocator_ ──────────► SpotAllocationStrategy
          │                                ▲          ▲
          │                          FirstAvailable  NearestToEntrance ...
          │
          │ holds  pricer_ ─────────────► PricingStrategy
          │                                ▲          ▲
          │                          FlatHourly   PerVehicleType ...
          └── parkVehicle() asks allocator_; unparkVehicle() asks pricer_
```

Implement **one** of each behind the interfaces (the task asks for one apiece).

**Allocation — first available compatible spot:**

```cpp
class FirstAvailableStrategy : public SpotAllocationStrategy {
public:
    // Walk floors in order, return the first free spot that fits the vehicle.
    // Returns nullptr if none — the lot turns that into a thrown rejection.
    ParkingSpot* findSpot(VehicleType v, const ParkingLot& lot) override;
    // (definition after ParkingLot is declared — it reads lot.floors())
};
```

**Pricing — flat hourly rate, rounding up partial hours:**

```cpp
class FlatHourlyPricing : public PricingStrategy {
public:
    explicit FlatHourlyPricing(double ratePerHour) : rate_(ratePerHour) {}

    double calculate(const Ticket& t) const override {
        auto now     = Clock::now();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                           now - t.entryTime).count();
        // Round partial hours UP — standard parking billing.
        long hours = (seconds + 3599) / 3600;
        if (hours < 1) hours = 1;            // minimum one hour
        return static_cast<double>(hours) * rate_;
    }
private:
    double rate_;
};
```

Note `PricingStrategy::calculate` takes only a `const Ticket&` — everything it needs (entry time, vehicle type, spot type) is reachable from the ticket. A *per-vehicle-type* pricer would switch on `t.vehicle.type`; a *day/night* pricer would look at `t.entryTime`. **Same interface, different policy** — that's the win the interviewer is probing.

> **Thinking habit:** when two requirements both say "pluggable," check whether they're independent. If neither policy needs the other's data, give each its own interface — never one mega-strategy.

---

## Step 5 — The concurrency answer: atomic claim, lock per floor

This is the non-functional differentiator. The danger is the classic check-then-act race:

```
Thread A: findSpot() -> spot #7 is free
Thread B: findSpot() -> spot #7 is free   (A hasn't claimed yet)
Thread A: occupy #7
Thread B: occupy #7                        // two cars, one spot — BUG
```

The fix: **the find-and-claim must be one atomic step.** Two clean ways to say it in an interview:

1. **Lock around the claim.** Take a mutex (one per floor keeps contention low), then *re-check* the spot is free and claim it under the lock. Cheap and obviously correct.
2. **Lock-free atomic flag.** Make occupancy a `std::atomic<bool>` and claim with `compare_exchange_strong(expected=false, desired=true)` — only the thread that flips `false→true` wins. No mutex, scales better.

We'll show the lock-per-floor version (easiest to reason about) and re-validate after locking, because `findSpot` ran *outside* the lock and the spot may have been taken meanwhile:

```cpp
class ParkingFloor {
public:
    explicit ParkingFloor(int number) : number_(number) {}

    void addSpot(std::unique_ptr<ParkingSpot> s) { spots_.push_back(std::move(s)); }
    const std::vector<std::unique_ptr<ParkingSpot>>& spots() const { return spots_; }

    // Lock guards every claim on THIS floor so the same spot can't go twice.
    std::mutex& mutex() { return mutex_; }
    int number() const  { return number_; }

private:
    int number_;
    std::vector<std::unique_ptr<ParkingSpot>> spots_;
    std::mutex mutex_;
};
```

> **Thinking habit:** "never assign the same spot twice" = make *find + claim* atomic. Either lock the smallest unit that contains the spot (per-floor mutex) or use a CAS on the occupancy flag. Re-validate inside the lock if the search ran outside it.

---

## Step 6 — Orchestrate with `ParkingLot`: park, unpark, reject

`ParkingLot` is the client. It owns the floors and *holds* the two strategies (injected). Its `parkVehicle` must, in order:

1. Ask the **allocation strategy** for a candidate spot.
2. If none → **reject** (throw).
3. **Claim it under the floor lock** (re-validate, in case it was taken). Retry/re-ask if the claim loses the race.
4. Issue and record a `Ticket`.

`unparkVehicle` must: look up the open ticket → ask the **pricing strategy** for the fee → free the spot → return a `Receipt`.

```cpp
class ParkingLot {
public:
    ParkingLot(std::vector<std::unique_ptr<ParkingFloor>> floors,
               std::unique_ptr<SpotAllocationStrategy>    allocator,
               std::unique_ptr<PricingStrategy>           pricer)
        : floors_(std::move(floors)),
          allocator_(std::move(allocator)),
          pricer_(std::move(pricer)) {}

    const std::vector<std::unique_ptr<ParkingFloor>>& floors() const { return floors_; }

    Ticket parkVehicle(const Vehicle& v) {
        // Re-ask the strategy until we actually win a claim (or run out).
        for (;;) {
            ParkingSpot* spot = allocator_->findSpot(v.type, *this);
            if (!spot)
                throw std::runtime_error("lot full for requested vehicle type");

            ParkingFloor* floor = floorOf(spot);
            std::lock_guard<std::mutex> lock(floor->mutex());

            // Re-validate under the lock: findSpot ran unlocked.
            if (!spot->isFree() || !spot->tryClaim())
                continue;                       // lost the race — ask again

            Ticket t{ nextTicketId_++, v, spot, Clock::now() };
            openTickets_[t.id] = t;
            return t;
        }
    }

    Receipt unparkVehicle(const Ticket& t) {
        auto it = openTickets_.find(t.id);
        if (it == openTickets_.end())
            throw std::invalid_argument("unknown or already-closed ticket");

        Ticket open = it->second;
        double fee  = pricer_->calculate(open);   // pricing strategy decides cost

        open.spot->release();                      // free the spot
        openTickets_.erase(it);
        return Receipt{ open, Clock::now(), fee };
    }

    bool isFull(VehicleType type) const {
        for (const auto& f : floors_)
            for (const auto& s : f->spots())
                if (s->fits(type) && s->isFree()) return false;
        return true;
    }

private:
    ParkingFloor* floorOf(ParkingSpot* spot) {
        for (const auto& f : floors_)
            for (const auto& s : f->spots())
                if (s.get() == spot) return f.get();
        return nullptr;
    }

    std::vector<std::unique_ptr<ParkingFloor>> floors_;
    std::unique_ptr<SpotAllocationStrategy>    allocator_;
    std::unique_ptr<PricingStrategy>           pricer_;
    std::unordered_map<int, Ticket>            openTickets_;
    int                                        nextTicketId_ = 1;
};

// Now that ParkingLot is fully declared, define the allocator that reads it.
ParkingSpot* FirstAvailableStrategy::findSpot(VehicleType v, const ParkingLot& lot) {
    for (const auto& floor : lot.floors())
        for (const auto& spot : floor->spots())
            if (spot->fits(v) && spot->isFree())
                return spot.get();
    return nullptr;
}
```

Two design wins to call out in an interview:
- **The lot never contains pricing or allocation logic** — it only *calls* the injected strategies. Swap a strategy in the constructor and behaviour changes with zero edits to `ParkingLot`. That's open/closed.
- **The retry loop + re-validate under lock** is the honest concurrency story: the unlocked search can go stale, so we confirm and claim atomically, and re-ask if we lost.

> **Thinking habit:** the client *coordinates* (find → claim → record); it *delegates* every policy decision. If `parkVehicle` ever grows a `switch` on vehicle type, a strategy leaked into the client.

---

## Step 7 — Prove it with a tiny driver

Always show a `main` that exercises a park, an unpark with a fee, and a rejection. It doubles as your test.

```cpp
#include <iostream>

int main() {
    // Build one floor: 1 motorcycle spot, 1 car spot.
    auto floor = std::make_unique<ParkingFloor>(1);
    floor->addSpot(std::make_unique<ParkingSpot>(101, VehicleType::MOTORCYCLE));
    floor->addSpot(std::make_unique<ParkingSpot>(102, VehicleType::CAR));

    std::vector<std::unique_ptr<ParkingFloor>> floors;
    floors.push_back(std::move(floor));

    // Inject one allocation strategy + one pricing strategy.
    ParkingLot lot(std::move(floors),
                   std::make_unique<FirstAvailableStrategy>(),
                   std::make_unique<FlatHourlyPricing>(40.0));   // 40 per hour

    // Park a car -> gets spot 102.
    Vehicle car{ "KA-01-1234", VehicleType::CAR };
    Ticket  t = lot.parkVehicle(car);
    std::cout << "Parked at spot " << t.spot->id() << "\n";   // 102

    // Second car -> no compatible spot left -> rejected, not crashed.
    try {
        lot.parkVehicle(Vehicle{ "KA-02-9999", VehicleType::CAR });
    } catch (const std::exception& e) {
        std::cout << "Rejected: " << e.what() << "\n";        // lot full ...
    }

    // Unpark -> fee computed by the pricing strategy, spot freed.
    Receipt r = lot.unparkVehicle(t);
    std::cout << "Fee: " << r.fee << "\n";                    // >= 40 (min 1 hr)

    return 0;
}
```

> **Thinking habit:** a driver that hits park, *reject-when-full*, and unpark-with-fee proves the happy path, the error path, and that both strategies actually fire — worth more than paragraphs.

---

## Step 8 — Talk through the follow-ups (don't necessarily code them all)

Show the seams are already there:

1. **EV-charging / handicapped-reserved spots.** Add spot subtypes (or an `attributes` field) and a new `SpotAllocationStrategy` that prefers/filters them (e.g. `EvFirstStrategy`). Compatibility moves into `fits()`. **New strategy + richer spot, the lot is untouched.**

2. **Reservation / pre-booking.** A reserved spot is one claimed ahead of arrival. Add a `reserve(spotId, window)` that flips occupancy with a future ticket; `findSpot` skips reserved spots. The atomic-claim primitive from Step 5 already prevents double-booking a reserved spot.

3. **Find-my-car / real-time availability.** The lot already knows every spot's occupancy. Index `plate → spot` on park for find-my-car; expose `availableCount(VehicleType, floor)` (a counter you maintain incrementally on claim/release) for a live display — same *incremental state* trick as any counter.

4. **Lost-ticket handling + flat penalty.** Since `unparkVehicle` delegates to a `PricingStrategy`, lost-ticket is just *another pricing path*: a `LostTicketPricing` (or a flag the pricer reads) that charges a flat max-day penalty. **No change to the lot** — the pricing axis already absorbs it.

> **Thinking habit:** good LLD answers end by mapping each follow-up onto an existing seam — "that's a new allocation strategy," "that's a new pricing strategy," "that's a counter." If a follow-up forces an edit to the client, your abstraction was wrong.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — two "must be pluggable" lines name **two Strategies**; "never assign a spot twice" names **atomic claim**.
2. **Nouns → classes**, one responsibility each (`Vehicle`, `ParkingSpot`, `Floor`, `Ticket`, `Receipt`, two strategies, `ParkingLot`).
3. **Interface first** — strategies arrive via the constructor; the lot depends on interfaces, not policies.
4. **Leaves first** (`Vehicle`, `ParkingSpot` with `tryClaim`/`release`), then the strategies, then the orchestrator.
5. **Two orthogonal Strategies** — allocation and pricing never reference each other.
6. **Concurrency = make find + claim atomic** (per-floor lock + re-validate, or CAS on the flag); retry if you lose the race.
7. **Client coordinates, strategies decide** — no `switch` on vehicle type inside `ParkingLot`.
8. **Driver as proof**, then **map every follow-up to a seam** (new strategy / counter), never an edit to the client.

Follow that skeleton on any "system with pluggable policies + a contended shared resource" LLD (ride-matching, seat booking, ad allocation) and the Strategy pattern plus the atomic-claim answer fall out almost mechanically.
