# Food Delivery (Swiggy / Zomato) — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **order State machine + pluggable matching Strategy + Observer** feel inevitable rather than bolted on.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design, not the feature list:

1. *"Order modeled as a **State machine**; invalid transitions rejected."* → The order's life is `PLACED → ACCEPTED → PREPARING → READY → PICKED_UP → DELIVERED` (+ `REJECTED / CANCELLED`). The core deliverable is a **guarded transition table**. A free-for-all `order.status = X` answer is a fail.
2. *"Delivery-partner assignment is a pluggable **Strategy** (nearest, batching)."* → Matching logic must sit behind an interface so you can swap *nearest* for *batching* without touching `OrderService`.
3. *"**Observer** for customer/restaurant/partner notifications."* → State changes fan out to subscribers; the order doesn't `cout` directly, it *publishes*.
4. *"Three independently-acting actors → assignment must be **atomic**."* → This is the sneaky one. Two orders must never grab the same partner. Guard the find-and-book step.

> **Thinking habit:** the prompt literally names three patterns (State, Strategy, Observer). Don't invent a fourth — map each named pattern to the requirement it solves, and the architecture is half-written.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *customer, restaurant, menu, item, cart, order, delivery partner, location, bill, status, notification.*

Group them by responsibility — entities, the orchestrator, and the swappable bits:

| Class | Owns | Why it exists |
|-------|------|---------------|
| `Location` | lat/lng (or x/y) | distance math for matching |
| `MenuItem` | id, name, price | a sellable line |
| `Restaurant` | id, location, cuisine, menu | what customers browse + the prep source |
| `Cart` | `{itemId → qty}` for one restaurant | the pre-order basket |
| `DeliveryPartner` | id, location, `available` flag | the mover; bookable resource |
| `Order` | id, actors, lines, **status**, partner | the **State machine** instance |
| `Bill` | items subtotal, tax, delivery fee | billing kept *out* of `Order` |
| `OrderObserver` (interface) | `onStatusChange(...)` | Observer subscriber role |
| `PartnerAssignmentStrategy` (interface) | `assign(order, partners)` | Strategy: how to pick a partner |
| `SearchService` | restaurant index | browse by location / cuisine |
| `OrderService` (the **orchestrator**) | orders, partners, strategy, observers | drives the lifecycle, guards transitions, books partners |

> **Thinking habit:** when actors act independently, separate the **entities** (dumb data + tiny invariants) from the **service** that coordinates them. Coordination is one class's job; don't smear it across the entities.

---

## Step 2 — Pin the public interface (the contract)

The statement hands us the shape — lock it before internals so the lifecycle commits you to a fixed set of verbs:

```cpp
enum class OrderStatus { PLACED, ACCEPTED, PREPARING, READY, PICKED_UP, DELIVERED, REJECTED, CANCELLED };

class PartnerAssignmentStrategy {                       // Strategy
public:
    virtual DeliveryPartner* assign(const Order&,
                                    std::vector<DeliveryPartner>&) = 0;
    virtual ~PartnerAssignmentStrategy() = default;
};

class OrderService {
public:
    Order placeOrder(const std::string& customerId,
                     const std::string& restaurantId, const Cart& cart);
    void  restaurantRespond(const std::string& orderId, bool accept);
    void  markReady(const std::string& orderId);        // triggers/locks-in partner assignment
    void  markPickedUp(const std::string& orderId);
    void  markDelivered(const std::string& orderId);
};

class SearchService {
public:
    std::vector<Restaurant> search(Location loc, const std::string& cuisine);
};
```

Decisions baked in here:
- **Each verb is one transition.** `restaurantRespond(accept=true)` is `PLACED → ACCEPTED`; `markReady` is `PREPARING → READY` *and* the moment we book a partner. The API *is* the state machine's edge list.
- **`assign` returns a pointer** so "no partner free" is naturally `nullptr` — and it takes the partner list *by non-const reference* because assigning must *mutate* (flip `available` to book atomically).
- **Invalid transition → throw.** Clean, testable, no half-progressed orders.

> **Thinking habit:** when each public method maps to exactly one edge of a state diagram, your interface is already correct. List the edges, name the methods after them.

---

## Step 3 — Model the leaves: `Location`, `MenuItem`, `Cart`, `Restaurant`, `DeliveryPartner`

Bottom-up: dependency-free types first.

```cpp
struct Location {
    double x = 0, y = 0;
    double distanceTo(const Location& o) const {
        double dx = x - o.x, dy = y - o.y;
        return std::sqrt(dx * dx + dy * dy);   // good enough; swap for haversine in prod
    }
};

struct MenuItem {
    std::string id;
    std::string name;
    int price = 0;     // integer minor-units (paise/cents) — never float money
};
```

`Cart` is a thin basket tied to one restaurant. It holds *quantities by item id*, not copies of items — the menu is the source of truth for price.

```cpp
class Cart {
public:
    std::string restaurantId;
    void add(const std::string& itemId, int qty) { lines_[itemId] += qty; }
    const std::unordered_map<std::string, int>& lines() const { return lines_; }
private:
    std::unordered_map<std::string, int> lines_;
};
```

`Restaurant` owns its menu and answers price lookups; `DeliveryPartner` is a bookable resource — note the single `available` flag, the hinge of atomic assignment.

```cpp
class Restaurant {
public:
    std::string id;
    std::string cuisine;
    Location    loc;

    void addItem(const MenuItem& m) { menu_[m.id] = m; }
    const MenuItem& item(const std::string& id) const { return menu_.at(id); }   // throws if missing
private:
    std::unordered_map<std::string, MenuItem> menu_;
};

struct DeliveryPartner {
    std::string id;
    Location    loc;
    bool        available = true;   // false once booked — the resource lock
};
```

> **Thinking habit:** money is integers, distance lives on `Location`, and the basket stores *ids + quantities*, never duplicated prices. Put each fact in exactly one place so it can't drift.

---

## Step 4 — The key insight: the order lifecycle is a guarded State machine

This is the heart of the problem. The order is not a struct with a mutable `status` field — it's an instance walking a fixed graph, and **every move is checked against a transition table.**

The legal edges:

```
PLACED ──accept──► ACCEPTED ──(auto)──► PREPARING ──ready──► READY
   │                                                            │
 reject                                                      assign+pickup
   ▼                                                            ▼
REJECTED                                                    PICKED_UP ──deliver──► DELIVERED

(CANCELLED reachable from PLACED / ACCEPTED / PREPARING — before pickup)
```

**Naive idea:** scatter `if (order.status == X) order.status = Y;` across `OrderService`. It works once, then the 9th method forgets a guard and you ship an order that's `DELIVERED` before it was `ACCEPTED`.

**The transition-table trick.** Encode legality as *data*, in one place, and check every move through one gate:

```cpp
class Order {
public:
    Order(std::string id, std::string customerId, std::string restaurantId)
        : id_(std::move(id)), customerId_(std::move(customerId)),
          restaurantId_(std::move(restaurantId)), status_(OrderStatus::PLACED) {}

    OrderStatus status() const { return status_; }
    const std::string& id() const { return id_; }
    const std::string& customerId() const { return customerId_; }
    const std::string& restaurantId() const { return restaurantId_; }

    DeliveryPartner* partner() const { return partner_; }
    void setPartner(DeliveryPartner* p) { partner_ = p; }

    // The ONE gate every status change goes through.
    void transitionTo(OrderStatus next) {
        if (!isLegal(status_, next))
            throw std::logic_error("illegal transition: "
                + std::to_string(int(status_)) + " -> " + std::to_string(int(next)));
        status_ = next;
    }

private:
    static bool isLegal(OrderStatus from, OrderStatus to) {
        static const std::map<OrderStatus, std::vector<OrderStatus>> table = {
            {OrderStatus::PLACED,    {OrderStatus::ACCEPTED, OrderStatus::REJECTED, OrderStatus::CANCELLED}},
            {OrderStatus::ACCEPTED,  {OrderStatus::PREPARING, OrderStatus::CANCELLED}},
            {OrderStatus::PREPARING, {OrderStatus::READY, OrderStatus::CANCELLED}},
            {OrderStatus::READY,     {OrderStatus::PICKED_UP}},
            {OrderStatus::PICKED_UP, {OrderStatus::DELIVERED}},
            // DELIVERED / REJECTED / CANCELLED are terminal -> no outgoing edges
        };
        auto it = table.find(from);
        if (it == table.end()) return false;                 // terminal state
        return std::find(it->second.begin(), it->second.end(), to) != it->second.end();
    }

    std::string id_, customerId_, restaurantId_;
    OrderStatus status_;
    DeliveryPartner* partner_ = nullptr;   // non-owning; OrderService owns the pool
};
```

Why this wins: terminal states have *no* outgoing edges, so a delivered order physically cannot move again. Adding a state later means adding *one row*, not auditing every method.

> **Thinking habit:** when something has a lifecycle, make legality a lookup table checked by a single `transitionTo`. Centralize the guard once; never write `status = X` in the wild.

---

## Step 5 — Billing kept separate: `Bill` and a `BillCalculator`

The interviewer explicitly wants *"billing separated out."* The order knows *what* was bought; a calculator knows *how to price it*. Keep pricing out of `Order` so a discount engine can slot in later (follow-up 3).

```cpp
struct Bill {
    int subtotal    = 0;
    int tax         = 0;
    int deliveryFee = 0;
    int total() const { return subtotal + tax + deliveryFee; }
};

class BillCalculator {
public:
    // Price the cart against the restaurant's live menu.
    static Bill compute(const Cart& cart, const Restaurant& r) {
        Bill b;
        for (auto& [itemId, qty] : cart.lines())
            b.subtotal += r.item(itemId).price * qty;
        b.tax         = b.subtotal * 5 / 100;   // 5% — a real engine would be pluggable
        b.deliveryFee = 3000;                    // flat 30.00
        return b;
    }
};
```

`compute` is `static` because pricing is a pure function of (cart, menu) today. The moment coupons arrive, this becomes an injectable `PricingStrategy` — same Strategy move as partner assignment.

> **Thinking habit:** anything that smells like a *policy* (tax %, fee, discount) is a future Strategy. Isolate it in its own function now so promoting it to an interface later is a one-line refactor.

---

## Step 6 — The Strategy: pluggable partner assignment (and atomic booking)

Matching is the second named pattern. The interface is given; the *body* is where you prove you understood "atomic."

```cpp
class PartnerAssignmentStrategy {
public:
    virtual DeliveryPartner* assign(const Order& order,
                                    std::vector<DeliveryPartner>& partners) = 0;
    virtual ~PartnerAssignmentStrategy() = default;
};
```

The default concrete strategy: **nearest available partner to the restaurant.** Booking is the find-the-min *and* flip-`available` step — and it must be one indivisible action so two orders can't both pick the same person.

```cpp
class NearestPartnerStrategy : public PartnerAssignmentStrategy {
public:
    explicit NearestPartnerStrategy(const Restaurant* r) : restaurant_(r) {}

    DeliveryPartner* assign(const Order&,
                            std::vector<DeliveryPartner>& partners) override {
        DeliveryPartner* best = nullptr;
        double bestDist = std::numeric_limits<double>::max();
        for (auto& p : partners) {
            if (!p.available) continue;
            double d = p.loc.distanceTo(restaurant_->loc);
            if (d < bestDist) { bestDist = d; best = &p; }
        }
        if (best) best->available = false;   // BOOK it inside the critical section
        return best;                         // nullptr => nobody free
    }
private:
    const Restaurant* restaurant_;
};
```

The atomicity note to say out loud: in a threaded system the *scan-then-book* above is a race — two threads can both read `best` before either flips `available`. The fix is to make `assign` run under `OrderService`'s lock (Step 7), so find-and-book is one critical section. `available = false` *is* the resource lock; the mutex just makes acquiring it atomic.

> **Thinking habit:** "atomic assignment" = the *select* and the *reserve* must be inseparable. Whenever you pick a shared resource and mark it taken, ask "can someone slip between those two lines?" — if yes, wrap both in one lock.

---

## Step 7 — The Observer: notify the three actors on every change

Third named pattern. The order publishes status changes; subscribers react. The order code never knows *who* listens — that's the whole point.

```cpp
class OrderObserver {
public:
    virtual void onStatusChange(const Order& order, OrderStatus newStatus) = 0;
    virtual ~OrderObserver() = default;
};

// Example concrete observers — one per actor.
class CustomerNotifier : public OrderObserver {
public:
    void onStatusChange(const Order& o, OrderStatus s) override {
        std::cout << "[customer " << o.customerId() << "] order " << o.id()
                  << " is now " << int(s) << "\n";
    }
};

class PartnerNotifier : public OrderObserver {
public:
    void onStatusChange(const Order& o, OrderStatus s) override {
        if (s == OrderStatus::READY && o.partner())
            std::cout << "[partner " << o.partner()->id << "] pickup ready for "
                      << o.id() << "\n";
    }
};
```

> **Thinking habit:** Observer decouples *when something happened* from *who cares*. The publisher iterates a list of `OrderObserver*`; adding a new listener (analytics, SMS) is a new subscriber, never an edit to the order.

---

## Step 8 — Orchestrate with `OrderService`: validate → transition → assign → notify

`OrderService` is where the three patterns meet. Every method follows the same rhythm: **fetch the order, transition through the guarded gate, do side effects (assign / bill), then publish.** A single mutex makes the whole step atomic across the three actors.

```cpp
class OrderService {
public:
    OrderService(std::vector<DeliveryPartner> partners,
                 std::unique_ptr<PartnerAssignmentStrategy> strategy)
        : partners_(std::move(partners)), strategy_(std::move(strategy)) {}

    void addObserver(OrderObserver* o) { observers_.push_back(o); }

    Order& placeOrder(const std::string& customerId,
                      const std::string& restaurantId, const Cart&) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::string id = "ORD-" + std::to_string(++counter_);
        auto [it, _] = orders_.emplace(id, Order(id, customerId, restaurantId));
        publish(it->second, OrderStatus::PLACED);   // already PLACED at construction
        return it->second;
    }

    void restaurantRespond(const std::string& orderId, bool accept) {
        std::lock_guard<std::mutex> lock(mtx_);
        Order& o = find(orderId);
        if (accept) {
            o.transitionTo(OrderStatus::ACCEPTED);   publish(o, OrderStatus::ACCEPTED);
            o.transitionTo(OrderStatus::PREPARING);  publish(o, OrderStatus::PREPARING);
        } else {
            o.transitionTo(OrderStatus::REJECTED);   publish(o, OrderStatus::REJECTED);
        }
    }

    void markReady(const std::string& orderId) {
        std::lock_guard<std::mutex> lock(mtx_);    // assignment runs inside this lock => atomic
        Order& o = find(orderId);
        o.transitionTo(OrderStatus::READY);

        DeliveryPartner* p = strategy_->assign(o, partners_);   // find + book, atomic
        if (!p) throw std::runtime_error("no delivery partner available");
        o.setPartner(p);
        publish(o, OrderStatus::READY);
    }

    void markPickedUp(const std::string& orderId) {
        std::lock_guard<std::mutex> lock(mtx_);
        Order& o = find(orderId);
        o.transitionTo(OrderStatus::PICKED_UP);  publish(o, OrderStatus::PICKED_UP);
    }

    void markDelivered(const std::string& orderId) {
        std::lock_guard<std::mutex> lock(mtx_);
        Order& o = find(orderId);
        o.transitionTo(OrderStatus::DELIVERED);  publish(o, OrderStatus::DELIVERED);
        if (o.partner()) o.partner()->available = true;   // free the partner
    }

private:
    Order& find(const std::string& id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) throw std::invalid_argument("unknown order: " + id);
        return it->second;
    }
    void publish(const Order& o, OrderStatus s) {
        for (auto* obs : observers_) obs->onStatusChange(o, s);
    }

    std::mutex mtx_;
    std::map<std::string, Order> orders_;
    std::vector<DeliveryPartner> partners_;                 // the pool we own + book
    std::unique_ptr<PartnerAssignmentStrategy> strategy_;   // injected — swappable
    std::vector<OrderObserver*> observers_;                 // non-owning subscribers
    int counter_ = 0;
};
```

Three design wins to call out in an interview:
- **`transitionTo` is the only mutator of status** — invalid transitions die at the gate, no matter which actor calls in.
- **Strategy is injected** (`unique_ptr<PartnerAssignmentStrategy>`) — swap `NearestPartnerStrategy` for a batching one and `OrderService` doesn't change.
- **The lock spans transition + assignment + notify** — the three independent actors can hammer the service concurrently and no partner gets double-booked.

> **Thinking habit:** every orchestrator method is the same loop — *validate (guarded transition) → mutate side effects → publish*. Fix that rhythm and the methods write themselves; the only variation is which edge and which side effect.

---

## Step 9 — Prove it with a tiny driver

Always show a `main` that walks the full happy path *and* hits a rejected transition. It doubles as your test.

```cpp
int main() {
    Restaurant r; r.id = "R1"; r.cuisine = "indian"; r.loc = {0, 0};
    r.addItem({"M1", "Paneer Tikka", 25000});

    std::vector<DeliveryPartner> partners = {
        {"P1", {10, 10}, true},
        {"P2", {1, 1},   true},   // closest to R1 -> should be booked
    };

    OrderService svc(std::move(partners),
                     std::make_unique<NearestPartnerStrategy>(&r));
    CustomerNotifier cust; PartnerNotifier part;
    svc.addObserver(&cust); svc.addObserver(&part);

    Cart cart; cart.restaurantId = "R1"; cart.add("M1", 2);
    Bill bill = BillCalculator::compute(cart, r);
    std::cout << "Bill total: " << bill.total() << "\n";   // 2*25000 + tax + fee

    Order& o = svc.placeOrder("C1", "R1", cart);           // PLACED
    svc.restaurantRespond(o.id(), true);                   // ACCEPTED -> PREPARING
    svc.markReady(o.id());                                 // READY + books P2
    svc.markPickedUp(o.id());                              // PICKED_UP
    svc.markDelivered(o.id());                             // DELIVERED, frees P2

    // Illegal transition now throws — DELIVERED is terminal.
    try { svc.markPickedUp(o.id()); }
    catch (const std::exception& e) { std::cout << "Rejected: " << e.what() << "\n"; }
    return 0;
}
```

> **Thinking habit:** a driver that walks the entire lifecycle once *and* attempts one illegal move proves both the edges and the guard — worth more than paragraphs of prose.

---

## Step 10 — Talk through the follow-ups (batching is the headline)

Show the seams are already there; name the pattern each follow-up reuses.

1. **Order batching (one partner, multiple nearby orders).** This is *just another Strategy*. Write `BatchingStrategy : PartnerAssignmentStrategy` that, instead of booking a fresh partner, looks for one already carrying an order whose route passes near this restaurant, and appends to their batch (so `available` becomes a *capacity counter*, not a bool). `OrderService` is untouched — inject the new strategy. That's the open/closed payoff of putting matching behind an interface.

2. **ETA estimation.** Add an `EtaEstimator` that sums *restaurant prep time* (per-restaurant attribute) + *travel time* (`partner.loc.distanceTo(restaurant) / speed` + delivery leg). Publish the ETA in the `READY`/`PICKED_UP` notifications via the existing Observer channel — no new plumbing.

3. **Coupons / pluggable pricing.** Promote `BillCalculator::compute` to a `PricingStrategy` interface (`Bill price(cart, restaurant, coupon)`), with `FlatTaxPricing`, `CouponPricing`, etc. Same Strategy move as assignment; billing was already separated out (Step 5), so this is a lift-and-shift.

4. **Live tracking & mid-delivery reassignment.** Partner cancels while `PICKED_UP`? Add a `partnerCancel(orderId)` edge that frees the old partner (`available = true`), keeps the order's status, and re-runs `strategy_->assign` under the lock to book a replacement — then publishes via Observer. The transition table gains one self-loop-ish recovery edge; everything else is reuse.

> **Thinking habit:** good LLD answers end by mapping each follow-up onto an *existing* seam — "that's a new Strategy," "that's a new Observer," "that's one more row in the table." If a follow-up forces an edit to the orchestrator, your abstractions were too shallow.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — the prompt named State, Strategy, Observer, and "atomic." Map each pattern to its requirement.
2. **Nouns → classes**, separating dumb **entities** from the coordinating **service**.
3. **Interface first** — each public verb is one edge of the lifecycle graph.
4. **Leaves first** (`Location`, `MenuItem`, `Cart`, `Restaurant`, `DeliveryPartner`); money as integers, distance on `Location`.
5. **Lifecycle = guarded transition table**, with a single `transitionTo` gate; terminal states have no outgoing edges.
6. **Billing separated** into its own calculator — a Strategy-in-waiting.
7. **Matching behind a Strategy**; *select + reserve* is one atomic critical section (`available` flag + lock).
8. **Observer fans out** status changes; publisher never knows its subscribers.
9. **Orchestrator rhythm**: validate (guarded transition) → mutate side effects → publish, all under one lock.
10. **Follow-ups = new Strategy / new Observer / one new table row**, never a rewrite.

Follow that skeleton on any "multi-actor system with a lifecycle and pluggable policy" LLD (ride-hailing, ticket booking, logistics) and the State + Strategy + Observer trio falls out almost mechanically.
