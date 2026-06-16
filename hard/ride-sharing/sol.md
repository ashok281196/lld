# Ride-Sharing (Uber / Ola) — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **two interlocking state machines + pluggable strategies + observer** design feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"Driver and Trip modeled as **State machines**… kept consistent."* → There are **two** machines (driver availability vs trip lifecycle) and they must never disagree. A driver going `BUSY` and a trip going `DRIVER_ASSIGNED` are the *same event* seen from two angles. Keeping them in lock-step is **the** correctness story.
2. *"Matching must be **atomic** — one driver must not be assigned to two trips."* → This is the concurrency trap. Selecting a driver and marking them busy must be **one indivisible step** under a lock. A "find then assign" with a gap is a fail.
3. *"**Matching strategy** pluggable (nearest / highest-rated / least-idle)."* and *"**Pricing strategy** pluggable (normal vs surge)."* → Two **Strategy** seams, given to us explicitly in the interface. Don't hardcode either.
4. *"**Notify** rider and driver on each state change."* → **Observer** pattern, fired from inside the trip's transitions.
5. Follow-ups name the scale seams: geospatial indexing, cancellation fees, pooled rides, ratings/ETA.

> **Thinking habit:** when a prompt lists *named patterns* (State, Strategy, Observer) in its constraints, it's handing you the skeleton. Your job is to wire them together cleanly, not to invent architecture.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *rider, driver, trip, request, location, ride type, fare, receipt, match, surge, notification, service.*

Group them into responsibilities:

| Class | Owns | Why it exists |
|-------|------|---------------|
| `Location` | lat / lng | a point; distance math |
| `RideType` (enum) | Mini / Sedan / SUV | tier selection + fare multiplier |
| `Rider` | id, name | who requests |
| `Driver` | id, location, rating, **availability state** | who serves; one of the two state machines |
| `RideRequest` | rider, pickup, drop, type | an immutable ask, fed to the matcher |
| `Trip` | rider, driver, route, **trip status**, fare | the lifecycle state machine + the unit of money |
| `Receipt` | trip id, fare, breakdown | the completion artifact |
| `MatchingStrategy` (interface) | "pick a driver" rule | pluggable matching |
| `PricingStrategy` (interface) | "compute fare" rule | pluggable normal/surge |
| `TripObserver` (interface) | `onStatusChange` | notifications |
| `RideService` (the **orchestrator**) | drivers, trips, the two strategies, the matcher lock | wires everything, owns atomic matching |

> **Thinking habit:** when two nouns each carry a *status* that changes over time (`Driver`, `Trip`), you have two state machines. Name them now so you remember to keep them consistent later.

---

## Step 2 — Pin the public interface (the contract)

The statement hands us the shape. Lock it before internals — it forces you to decide what the outside world sees.

```cpp
enum class TripStatus { REQUESTED, DRIVER_ASSIGNED, ARRIVED, IN_PROGRESS, COMPLETED, CANCELLED };
enum class RideType   { MINI, SEDAN, SUV };

class MatchingStrategy {                          // Strategy
public:
    virtual Driver* match(const RideRequest&, const std::vector<Driver*>& available) = 0;
    virtual ~MatchingStrategy() = default;
};

class PricingStrategy {                           // Strategy (normal / surge)
public:
    virtual double fare(const Trip&) const = 0;
    virtual ~PricingStrategy() = default;
};

class RideService {
public:
    Trip&   requestRide(const std::string& riderId, Location pickup, Location drop, RideType type);
    void    acceptRide(const std::string& driverId, const std::string& tripId);
    void    arrive(const std::string& tripId);
    void    startTrip(const std::string& tripId);
    Receipt endTrip(const std::string& tripId);   // computes fare
    void    cancelTrip(const std::string& tripId, const std::string& byActor);
};
```

Decisions baked in here:
- **The matcher returns `Driver*`** (`nullptr` = no driver found). Natural "none" without exceptions.
- **`requestRide` synchronously attempts a match.** In the real world matching is async; for the LLD we match inline and return the `Trip` already `REQUESTED` (or `DRIVER_ASSIGNED` if a driver was found immediately — we'll do the offer/accept handshake explicitly via `acceptRide`).
- **Every lifecycle verb is a separate method** (`acceptRide`, `arrive`, `startTrip`, `endTrip`, `cancelTrip`) → each is one *transition* on the trip's state machine.

> **Thinking habit:** map each public method to exactly one state transition. If a method would fire two transitions, you're hiding a state.

---

## Step 3 — Model the leaves: `Location`, `RideType`, `Rider`, `RideRequest`, `Receipt`

Bottom-up — things with no dependencies first.

```cpp
struct Location {
    double lat = 0, lng = 0;
    // Cheap planar approximation; good enough for matching/fare in an LLD.
    double distanceTo(const Location& o) const {
        double dx = lat - o.lat, dy = lng - o.lng;
        return std::sqrt(dx * dx + dy * dy);
    }
};

enum class RideType { MINI, SEDAN, SUV };
// Per-tier multiplier; bigger car costs more per km.
inline double tierMultiplier(RideType t) {
    switch (t) {
        case RideType::MINI:  return 1.0;
        case RideType::SEDAN: return 1.3;
        case RideType::SUV:   return 1.7;
    }
    return 1.0;
}

struct Rider {
    std::string id;
    std::string name;
};

// Immutable description of an ask — the matcher reads it, never mutates it.
struct RideRequest {
    std::string riderId;
    Location    pickup;
    Location    drop;
    RideType    type;
    double tripDistance() const { return pickup.distanceTo(drop); }
};

struct Receipt {
    std::string tripId;
    double      fare = 0;
    double      distance = 0;
    double      surgeMultiplier = 1.0;
};
```

> **Thinking habit:** make the request object **immutable** (a value you pass to strategies). Strategies should *read* inputs and *return* decisions, never reach back and mutate shared state.

---

## Step 4 — The key insight: two state machines that move together

This is the heart of the problem. The trip lifecycle and the driver's availability are **coupled**: certain trip transitions *must* flip the driver's state, atomically.

**The driver machine** (availability):

```
OFFLINE ⇄ AVAILABLE ──assigned──► ON_TRIP ──trip ends/cancelled──► AVAILABLE
```

**The trip machine** (lifecycle), with the *side-effect on the driver* annotated:

```
REQUESTED ──acceptRide──► DRIVER_ASSIGNED   (driver: AVAILABLE → ON_TRIP)
DRIVER_ASSIGNED ──arrive──► ARRIVED
ARRIVED ──startTrip──► IN_PROGRESS
IN_PROGRESS ──endTrip──► COMPLETED          (driver: ON_TRIP → AVAILABLE, fare computed)
{any non-terminal} ──cancelTrip──► CANCELLED (driver: ON_TRIP → AVAILABLE if one was held)
```

The non-obvious rule: **a driver leaves `AVAILABLE` at assignment, and returns to `AVAILABLE` on exactly one of two terminal trip events (`COMPLETED` or `CANCELLED`).** Miss either return path and the driver is leaked — booked forever. So the consistency invariant is:

> *A driver is `ON_TRIP` **iff** they are referenced by exactly one trip whose status is in `{DRIVER_ASSIGNED, ARRIVED, IN_PROGRESS}`.*

We model the driver's availability with a small enum (the machine is simple enough that a full State-object hierarchy is overkill — call this out: *"I'd use State objects if the driver had rich per-state behaviour; here an enum + guarded transitions is leaner"*). The **trip** transitions are the interesting machine, and we centralize them so the driver side-effect can't drift.

```cpp
enum class DriverStatus { OFFLINE, AVAILABLE, ON_TRIP };

class Driver {
public:
    Driver(std::string id, Location loc, double rating)
        : id_(std::move(id)), loc_(loc), rating_(rating) {}

    const std::string& id() const { return id_; }
    Location location() const     { return loc_; }
    double   rating() const       { return rating_; }
    DriverStatus status() const   { return status_; }

    void goOnline()  { if (status_ == DriverStatus::OFFLINE)   status_ = DriverStatus::AVAILABLE; }
    void goOffline() { if (status_ == DriverStatus::AVAILABLE) status_ = DriverStatus::OFFLINE; }

    // Driven by RideService under the matcher lock — never call directly elsewhere.
    void markOnTrip()    { status_ = DriverStatus::ON_TRIP; }
    void markAvailable() { status_ = DriverStatus::AVAILABLE; }

    void setLocation(Location l) { loc_ = l; }

private:
    std::string  id_;
    Location     loc_;
    double       rating_ = 0;
    DriverStatus status_ = DriverStatus::OFFLINE;
};
```

> **Thinking habit:** when two machines are coupled, write the **invariant** that ties them ("driver is ON_TRIP iff an active trip references them"). Every transition you code afterward is just "preserve the invariant."

---

## Step 5 — Model the `Trip` and centralize its transitions

`Trip` holds the lifecycle state and the data fare needs (distance, type, surge). It exposes guarded transition methods — each rejects illegal moves, so the state machine is enforced **inside** the object, not by callers.

```cpp
class Trip {
public:
    Trip(std::string id, RideRequest req)
        : id_(std::move(id)), req_(std::move(req)) {}

    const std::string& id() const   { return id_; }
    TripStatus status() const       { return status_; }
    const RideRequest& request() const { return req_; }
    const std::string& driverId() const { return driverId_; }
    double distance() const         { return req_.tripDistance(); }
    RideType type() const           { return req_.type; }

    double fare() const             { return fare_; }
    void   setFare(double f)        { fare_ = f; }

    // ---- guarded transitions: legal move => advance, illegal => throw ----
    void assign(const std::string& driverId) {
        expect(TripStatus::REQUESTED, "assign");
        driverId_ = driverId;
        status_   = TripStatus::DRIVER_ASSIGNED;
    }
    void arrive() {
        expect(TripStatus::DRIVER_ASSIGNED, "arrive");
        status_ = TripStatus::ARRIVED;
    }
    void start() {
        expect(TripStatus::ARRIVED, "start");
        status_ = TripStatus::IN_PROGRESS;
    }
    void complete() {
        expect(TripStatus::IN_PROGRESS, "complete");
        status_ = TripStatus::COMPLETED;
    }
    void cancel() {
        if (isTerminal())
            throw std::logic_error("trip already finished; cannot cancel");
        status_ = TripStatus::CANCELLED;
    }

    bool isTerminal() const {
        return status_ == TripStatus::COMPLETED || status_ == TripStatus::CANCELLED;
    }
    // A driver is "held" while the trip is active (assigned but not terminal).
    bool holdsDriver() const {
        return !driverId_.empty() && !isTerminal();
    }

private:
    void expect(TripStatus s, const char* op) const {
        if (status_ != s)
            throw std::logic_error(std::string("illegal transition: ") + op);
    }

    std::string id_;
    RideRequest req_;
    std::string driverId_;
    TripStatus  status_ = TripStatus::REQUESTED;
    double      fare_   = 0;
};
```

Notice `Trip` knows its own legal moves but **nothing about drivers' availability or notifications** — `RideService` owns those side-effects. Clean separation, same discipline as keeping board-geometry out of game-rules.

> **Thinking habit:** put the "is this transition legal?" check *inside* the state-holding object (`expect`). Callers then can't corrupt the machine, and every illegal path fails loudly instead of silently.

---

## Step 6 — The Strategy seams: matching and pricing

Two pluggable behaviours, both given as interfaces. Each takes inputs and returns a decision — no shared mutable state.

**Matching** — same signature, different rule:

```cpp
class MatchingStrategy {
public:
    virtual ~MatchingStrategy() = default;
    // Pick a driver from `available`, or nullptr if none suitable.
    virtual Driver* match(const RideRequest& req,
                          const std::vector<Driver*>& available) = 0;
};

// Closest driver to the pickup point.
class NearestDriverStrategy : public MatchingStrategy {
public:
    Driver* match(const RideRequest& req,
                  const std::vector<Driver*>& available) override {
        Driver* best = nullptr;
        double  bestDist = std::numeric_limits<double>::max();
        for (Driver* d : available) {
            double dist = d->location().distanceTo(req.pickup);
            if (dist < bestDist) { bestDist = dist; best = d; }
        }
        return best;
    }
};

// Highest-rated driver (tie-break left to insertion order).
class HighestRatedStrategy : public MatchingStrategy {
public:
    Driver* match(const RideRequest&,
                  const std::vector<Driver*>& available) override {
        Driver* best = nullptr;
        for (Driver* d : available)
            if (!best || d->rating() > best->rating()) best = d;
        return best;
    }
};
```

**Pricing** — normal vs surge. Both read the `Trip`; surge just multiplies.

```cpp
class PricingStrategy {
public:
    virtual ~PricingStrategy() = default;
    virtual double fare(const Trip&) const = 0;
};

// base + per-km + per-tier multiplier.
class NormalPricing : public PricingStrategy {
public:
    double fare(const Trip& t) const override {
        const double base = 50.0, perKm = 12.0;
        return (base + perKm * t.distance()) * tierMultiplier(t.type());
    }
protected:
    double rawFare(const Trip& t) const { return fare(t); }
};

// Surge wraps the normal fare with a demand multiplier (decorator-ish).
class SurgePricing : public PricingStrategy {
public:
    SurgePricing(double multiplier) : surge_(multiplier) {}
    double fare(const Trip& t) const override {
        return base_.fare(t) * surge_;
    }
    double surge() const { return surge_; }
private:
    NormalPricing base_;
    double        surge_ = 1.0;
};
```

> **Thinking habit:** a Strategy method should be a *pure function of its inputs* — `match(request, drivers)` and `fare(trip)`. Purity is what makes them swappable and testable in isolation.

---

## Step 7 — The Observer seam: notifications on every state change

The interface is trivial; the discipline is *where you fire it* — from inside the service's transition handlers, so no transition can ship without notifying.

```cpp
class TripObserver {
public:
    virtual ~TripObserver() = default;
    virtual void onStatusChange(const Trip& trip, TripStatus from, TripStatus to) = 0;
};

// A concrete observer: push notifications (here just prints).
class NotificationService : public TripObserver {
public:
    void onStatusChange(const Trip& trip, TripStatus from, TripStatus to) override {
        std::cout << "[notify] trip " << trip.id()
                  << " : " << name(from) << " -> " << name(to) << "\n";
    }
private:
    static const char* name(TripStatus s) {
        switch (s) {
            case TripStatus::REQUESTED:       return "REQUESTED";
            case TripStatus::DRIVER_ASSIGNED: return "DRIVER_ASSIGNED";
            case TripStatus::ARRIVED:         return "ARRIVED";
            case TripStatus::IN_PROGRESS:     return "IN_PROGRESS";
            case TripStatus::COMPLETED:       return "COMPLETED";
            case TripStatus::CANCELLED:       return "CANCELLED";
        }
        return "?";
    }
};
```

> **Thinking habit:** fire observers from a *single choke point* per transition, never scattered at call sites. One place to fire = no missed notification and easy to add a second observer later.

---

## Step 8 — The orchestrator: `RideService` and atomic matching

This is where the two machines are kept consistent and where the **concurrency trap** is defused. `requestRide` must do "pick an available driver **and** mark them busy" as one indivisible step under a lock — otherwise two riders race onto the same driver.

```cpp
class RideService {
public:
    RideService(std::unique_ptr<MatchingStrategy> matcher,
                std::unique_ptr<PricingStrategy>  pricing)
        : matcher_(std::move(matcher)), pricing_(std::move(pricing)) {}

    void addDriver(Driver* d)            { drivers_.push_back(d); }
    void setPricing(std::unique_ptr<PricingStrategy> p) { pricing_ = std::move(p); }
    void subscribe(TripObserver* o)      { observers_.push_back(o); }

    Trip& requestRide(const std::string& riderId, Location pickup,
                      Location drop, RideType type) {
        RideRequest req{riderId, pickup, drop, type};
        std::string tripId = "T" + std::to_string(++counter_);
        auto trip = std::make_unique<Trip>(tripId, req);
        Trip& ref = *trip;
        trips_[tripId] = std::move(trip);

        // ---- ATOMIC: snapshot available drivers, pick one, lock him in ----
        {
            std::lock_guard<std::mutex> guard(matchMutex_);
            std::vector<Driver*> available;
            for (Driver* d : drivers_)
                if (d->status() == DriverStatus::AVAILABLE) available.push_back(d);

            Driver* chosen = matcher_->match(req, available);
            if (chosen) {
                chosen->markOnTrip();            // (1) flip driver  } both inside
                transition(ref, [&] { ref.assign(chosen->id()); }); // (2) flip trip } the lock
            }
        }
        return ref;   // REQUESTED if no driver, else DRIVER_ASSIGNED
    }

    void acceptRide(const std::string& driverId, const std::string& tripId) {
        // In a pure offer/accept model the driver confirms here. With the
        // synchronous matcher above the assignment already happened; this
        // method exists for the explicit handshake variant.
        (void)driverId; (void)tripId;
    }

    void arrive(const std::string& tripId) {
        Trip& t = trip(tripId);
        transition(t, [&] { t.arrive(); });
    }

    void startTrip(const std::string& tripId) {
        Trip& t = trip(tripId);
        transition(t, [&] { t.start(); });
    }

    Receipt endTrip(const std::string& tripId) {
        Trip& t = trip(tripId);
        double f = pricing_->fare(t);                 // Strategy decides fare
        t.setFare(f);
        transition(t, [&] { t.complete(); });
        releaseDriver(t);                              // driver back to AVAILABLE
        return Receipt{tripId, f, t.distance(), surgeOf()};
    }

    void cancelTrip(const std::string& tripId, const std::string& byActor) {
        Trip& t = trip(tripId);
        std::cout << "[cancel] " << tripId << " by " << byActor << "\n";
        transition(t, [&] { t.cancel(); });
        releaseDriver(t);                              // free the held driver, if any
    }

private:
    // Single choke point: run the mutation, then fire observers with from/to.
    template <typename Fn>
    void transition(Trip& t, Fn&& mutate) {
        TripStatus from = t.status();
        mutate();
        TripStatus to = t.status();
        if (from != to)
            for (TripObserver* o : observers_) o->onStatusChange(t, from, to);
    }

    void releaseDriver(Trip& t) {
        std::lock_guard<std::mutex> guard(matchMutex_);
        for (Driver* d : drivers_)
            if (d->id() == t.driverId()) { d->markAvailable(); break; }
    }

    Trip& trip(const std::string& id) {
        auto it = trips_.find(id);
        if (it == trips_.end()) throw std::invalid_argument("no such trip: " + id);
        return *it->second;
    }
    double surgeOf() const {
        if (auto* s = dynamic_cast<SurgePricing*>(pricing_.get())) return s->surge();
        return 1.0;
    }

    std::unique_ptr<MatchingStrategy> matcher_;
    std::unique_ptr<PricingStrategy>  pricing_;
    std::vector<Driver*>              drivers_;     // not owned here
    std::unordered_map<std::string, std::unique_ptr<Trip>> trips_;
    std::vector<TripObserver*>        observers_;
    std::mutex                        matchMutex_;
    int                               counter_ = 0;
};
```

Three design wins to call out in an interview:
- **Atomic matching.** Snapshot-available → `match` → `markOnTrip` → `assign` all sit inside one `lock_guard`. No window where two requests see the same `AVAILABLE` driver. `releaseDriver` takes the same lock so availability flips are serialized.
- **One transition choke point.** Every lifecycle change runs through `transition(...)`, which is the *only* place observers fire. Add a logging observer tomorrow and zero call sites change.
- **The two machines flip together.** Driver `markOnTrip()` and trip `assign()` are adjacent under the same lock; `releaseDriver` is invoked on *both* terminal paths (`endTrip`, `cancelTrip`), preserving the Step-4 invariant.

> **Thinking habit:** the moment two state changes *must* agree, put them adjacent inside one critical section. "Atomic" in an LLD answer means "no observable in-between state," and a lock around the pair is how you say that in code.

---

## Step 9 — Prove it with a driver

Always show a `main` that runs a full happy lifecycle, swaps in surge pricing, and exercises a rejected transition. It doubles as your test.

```cpp
#include <iostream>

int main() {
    Driver alice("D1", {0.0, 0.0}, 4.9);
    Driver bob  ("D2", {5.0, 5.0}, 4.5);
    alice.goOnline();
    bob.goOnline();

    RideService svc(std::make_unique<NearestDriverStrategy>(),
                    std::make_unique<NormalPricing>());
    NotificationService notifier;
    svc.subscribe(&notifier);
    svc.addDriver(&alice);
    svc.addDriver(&bob);

    // Rider near (0,0): NearestDriverStrategy should pick Alice.
    Trip& trip = svc.requestRide("R1", {0.1, 0.1}, {3.0, 4.0}, RideType::SEDAN);
    std::cout << "assigned driver: " << trip.driverId() << "\n";   // D1

    svc.arrive(trip.id());        // DRIVER_ASSIGNED -> ARRIVED
    svc.startTrip(trip.id());     // ARRIVED -> IN_PROGRESS

    // Switch to surge before completing — Strategy swap at runtime.
    svc.setPricing(std::make_unique<SurgePricing>(1.8));
    Receipt r = svc.endTrip(trip.id());   // IN_PROGRESS -> COMPLETED
    std::cout << "fare: " << r.fare << " (surge x" << r.surgeMultiplier << ")\n";

    // Alice is AVAILABLE again — invariant held.
    std::cout << "alice available? "
              << (alice.status() == DriverStatus::AVAILABLE) << "\n";

    // Illegal transition: completed trip can't be started again.
    try {
        svc.startTrip(trip.id());
    } catch (const std::exception& e) {
        std::cout << "rejected: " << e.what() << "\n";
    }
    return 0;
}
```

> **Thinking habit:** a driver that walks the full state path, swaps a strategy live, *and* hits one illegal transition proves the machine, the Strategy seam, and the guard rails in ~25 lines.

---

## Step 10 — Talk through the follow-ups (don't necessarily code them all)

Show the seams are already there:

1. **Geospatial matching at scale.** Today the matcher linearly scans `available` drivers — `O(n)`. At city scale you can't scan every driver. Index driver locations in a **geohash / grid bucket / quadtree**, query only the cells near the pickup, and feed *that* short list to the same `MatchingStrategy`. The Strategy interface is untouched — only the candidate set shrinks. Name the trade-off: grid is simplest, quadtree adapts to density.
2. **Cancellation policy and fees.** `cancelTrip` already records *who* cancelled (`byActor`) and the trip knows its current status. A `CancellationPolicy` strategy can read `(actor, status, elapsed)` and return a fee — e.g. free before `ARRIVED`, charged after. Another Strategy, no machine change.
3. **Pool / shared rides.** A `Trip` becomes a `Trip` with a *list* of riders and waypoints; the driver machine is unchanged (still one `ON_TRIP` driver), the trip machine gains intermediate pickup/drop sub-states. The matcher gains a "can this trip absorb another rider on its route?" question — a new `MatchingStrategy` variant.
4. **Ratings, payout, ETA.** Ratings are an Observer that updates `Driver.rating_` on `COMPLETED` (which then feeds `HighestRatedStrategy`). ETA is a function of pickup distance + traffic, computed at assignment and re-fired through the *same* observer channel.

> **Thinking habit:** good LLD answers end by mapping each follow-up onto an existing seam ("that's another Strategy", "that's another Observer", "that's just a smaller candidate set"). It proves your abstractions weren't accidental.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — "two state machines, kept consistent" and "matching must be atomic" decide the whole design.
2. **Nouns → classes**, one responsibility each; spot the *two* status-bearing nouns (`Driver`, `Trip`) as the two machines.
3. **Interface first** — map each public method to exactly one transition.
4. **Leaves first** (`Location`, `RideType`, `Rider`, `RideRequest`, `Receipt`), keep the request immutable.
5. **Write the coupling invariant** ("driver ON_TRIP iff an active trip references them"); every transition just preserves it.
6. **Guard transitions inside the state object** (`Trip::expect`) so callers can't corrupt the machine.
7. **Strategies are pure functions of inputs** (`match`, `fare`) → swappable, testable.
8. **Observers fire from one choke point** per transition.
9. **Atomic = adjacent mutations under one lock** — driver-flip and trip-assign together; release on *both* terminal paths.
10. **Driver as proof**, then **map follow-ups onto existing seams** (geohash candidate set, cancellation Strategy, pool sub-states, rating Observer).

Follow that skeleton on any "marketplace that matches supply to demand through a lifecycle" LLD (food delivery, logistics dispatch, hotel booking) and the State + Strategy + Observer trio falls out almost mechanically.
