# Vending Machine — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **State pattern** feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"Behaviour must be driven by **state objects**, not a giant `if/switch` on an enum."* → This is **the** thing being tested. A `switch (currentState)` answer is a fail. The whole solution is a textbook **State pattern**.
2. *"Each state defines its own response to `insertCoin`, `selectProduct`, `dispense`, `cancel`."* → Every state implements the *same four operations*; the difference is *how* each reacts. That's a base class with 4 virtual methods.
3. *"Illegal operations in a given state are handled by that state"* (e.g. `dispense()` while `Idle`). → Rejection logic lives **inside** each state, not in the machine.
4. Follow-ups name the seams: change-making (a small greedy algorithm), concurrency, pluggable payment.

> **Thinking habit:** when the prompt says "states… not if/switch," it's literally naming the pattern. Build the pattern, don't fight it.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *machine, product, slot, coin, money, change, state, admin, cash.*

| Class | Owns | Why it exists |
|-------|------|---------------|
| `Coin` (enum) | denomination value | the money unit |
| `Product` | code, name, price | what's for sale |
| `Inventory` | products + quantities | stock-keeping, refill |
| `State` (interface) | the 4 operations | one reaction-set per machine mode |
| `IdleState` / `HasMoneyState` / `DispensingState` | transition rules for that mode | the actual behaviours |
| `VendingMachine` (the **Context**) | current state + balance + inventory + cash | delegates every call to its state |

> **Thinking habit:** State pattern = one **Context** (the machine) + one **State interface** + N **concrete states**. Spot those three roles and the class list writes itself.

---

## Step 2 — Pin the public interface (the contract)

Given to us — lock it before internals:

```cpp
enum class Coin { ONE, FIVE, TEN, TWENTY };

class VendingMachine {
public:
    void    selectProduct(const std::string& code);
    void    insertCoin(Coin c);
    Product dispense();        // valid only in Dispensing state
    void    cancel();          // refund all inserted money, return to Idle

    // admin
    void refill(const std::string& code, int qty);
    int  collectCash();
};
```

Key realization: **none of these methods contain logic.** Each just forwards to the current state:

```cpp
void selectProduct(const std::string& code) { state_->selectProduct(*this, code); }
```

The machine is a *dispatcher*. All real behaviour lives in the states. That single idea is the whole pattern.

> **Thinking habit:** in State pattern the Context's public methods are one-liners that delegate. If yours have `if`s in them, the logic leaked out of the states.

---

## Step 3 — Model the leaves: `Coin`, `Product`, `Inventory`

Bottom-up: things with no dependencies first.

```cpp
// Give each coin its actual value so change-making is trivial later.
enum class Coin { ONE = 1, FIVE = 5, TEN = 10, TWENTY = 20 };
inline int valueOf(Coin c) { return static_cast<int>(c); }

struct Product {
    std::string code;
    std::string name;
    int price = 0;   // in the same units as coins
};
```

`Inventory` hides "code → (product, qty)". The machine asks it questions; it never exposes the raw map (encapsulation — same rule as the board's private grid).

```cpp
class Inventory {
public:
    void add(const Product& p, int qty) {
        auto it = entries_.find(p.code);
        if (it == entries_.end()) entries_[p.code] = {p, qty};
        else                      it->second.qty += qty;
    }
    bool hasStock(const std::string& code) const {
        auto it = entries_.find(code);
        return it != entries_.end() && it->second.qty > 0;
    }
    const Product& product(const std::string& code) const {
        return entries_.at(code).product;   // throws if missing
    }
    void reduceOne(const std::string& code) { entries_.at(code).qty -= 1; }

private:
    struct Entry { Product product; int qty; };
    std::unordered_map<std::string, Entry> entries_;
};
```

> **Thinking habit:** give `Coin` a real integer value now; it turns change-making from a lookup table into one `static_cast`.

---

## Step 4 — The key insight: the State pattern shape

Three roles, wired in a triangle:

```
  VendingMachine (Context)
        │ holds  state_ ──────────────► State (interface)
        │ exposes helpers                ▲   ▲   ▲
        │ (balance, inventory,           │   │   │
        │  setState, refund...)     Idle  HasMoney  Dispensing
        └── states call those helpers to drive the machine ◄──┘
```

- **Context** holds the current `State*` and the *data* (balance, inventory, cash, selection).
- **States are stateless** — they hold no per-machine data. So we can make them shared/owned-once objects. Each state's methods take `VendingMachine&` and read/mutate it through helper methods.
- **Transitions are owned by the states.** A state decides what happens next and calls `machine.setState(...)`. The machine never decides transitions itself.

The base interface (given):

```cpp
class VendingMachine;   // forward declaration — states reference it

class State {
public:
    virtual ~State() = default;
    virtual void    selectProduct(VendingMachine&, const std::string&) = 0;
    virtual void    insertCoin(VendingMachine&, Coin) = 0;
    virtual Product dispense(VendingMachine&) = 0;
    virtual void    cancel(VendingMachine&) = 0;
    virtual std::string name() const = 0;   // handy for errors/debugging
};
```

> **Thinking habit:** the rule of thumb for "who owns the transition" — **the state you're leaving decides where you go next.** That keeps the machine dumb and the states cohesive.

---

## Step 5 — The Context: `VendingMachine` holds data + helpers states drive

The machine needs to expose *just enough* for states to do their job: read balance, change balance, touch inventory, switch state, bank cash. These helpers are the machine's "internal API for its states."

```cpp
class VendingMachine {
public:
    VendingMachine();   // defined after the states exist (see Step 6)

    // ---- public user API: pure delegation ----
    void    selectProduct(const std::string& code) { state_->selectProduct(*this, code); }
    void    insertCoin(Coin c)                      { state_->insertCoin(*this, c); }
    Product dispense()                              { return state_->dispense(*this); }
    void    cancel()                                { state_->cancel(*this); }

    // ---- admin ----
    void refill(const std::string& code, int qty) { inventory_.add({code, code, 0}, qty); }
    int  collectCash() { int c = cashBox_; cashBox_ = 0; return c; }

    // ---- helpers the STATES use to drive the machine ----
    void setState(State* s) { state_ = s; }
    State* idle();        // accessors to the three state singletons
    State* hasMoney();
    State* dispensing();

    Inventory& inventory()              { return inventory_; }
    int   balance() const               { return balance_; }
    void  addBalance(int v)             { balance_ += v; }
    int   refundBalance() { int b = balance_; balance_ = 0; return b; } // returns coins to user
    void  bankBalance()   { cashBox_ += balance_; balance_ = 0; }       // keep the sale money
    const std::string& selected() const { return selectedCode_; }
    void  setSelected(const std::string& code) { selectedCode_ = code; }

private:
    std::unique_ptr<State> idle_, hasMoney_, dispensing_;  // machine OWNS the states
    State* state_ = nullptr;                               // current (non-owning view)
    Inventory   inventory_;
    int         balance_ = 0;     // coins inserted this session
    int         cashBox_ = 0;     // accumulated takings (admin collects)
    std::string selectedCode_;
};
```

> **Thinking habit:** split methods into *user API* (delegates) and *state-driver helpers*. The helpers are why states can stay tiny and data-free.

---

## Step 6 — Implement the three states (each rejects what it can't do)

This is where the "illegal op handled by the state" requirement lives. Each state answers all four calls — valid ones do work + transition, invalid ones reject cleanly.

```cpp
// ---------- Idle: nothing selected, no money ----------
class IdleState : public State {
public:
    void selectProduct(VendingMachine& m, const std::string& code) override {
        if (!m.inventory().hasStock(code))
            throw std::invalid_argument("out of stock: " + code);
        m.setSelected(code);
        m.setState(m.hasMoney());          // transition Idle -> HasMoney
    }
    void insertCoin(VendingMachine&, Coin) override {
        throw std::logic_error("select a product first");
    }
    Product dispense(VendingMachine&) override {
        throw std::logic_error("nothing selected");
    }
    void cancel(VendingMachine&) override { /* nothing to refund, stay Idle */ }
    std::string name() const override { return "Idle"; }
};

// ---------- HasMoney: product chosen, accepting coins ----------
class HasMoneyState : public State {
public:
    void selectProduct(VendingMachine&, const std::string&) override {
        throw std::logic_error("already selecting; cancel to change product");
    }
    void insertCoin(VendingMachine& m, Coin c) override {
        m.addBalance(valueOf(c));
        int price = m.inventory().product(m.selected()).price;
        if (m.balance() >= price)
            m.setState(m.dispensing());    // enough money -> ready to vend
    }
    Product dispense(VendingMachine&) override {
        throw std::logic_error("insufficient funds");
    }
    void cancel(VendingMachine& m) override {
        int refund = m.refundBalance();
        std::cout << "Refunded " << refund << "\n";
        m.setSelected("");
        m.setState(m.idle());              // HasMoney -> Idle
    }
    std::string name() const override { return "HasMoney"; }
};

// ---------- Dispensing: paid in full, ready to drop product ----------
class DispensingState : public State {
public:
    void selectProduct(VendingMachine&, const std::string&) override {
        throw std::logic_error("dispensing in progress");
    }
    void insertCoin(VendingMachine&, Coin) override {
        throw std::logic_error("dispensing in progress");
    }
    Product dispense(VendingMachine& m) override {
        const Product& p = m.inventory().product(m.selected());
        int change = m.balance() - p.price;

        m.inventory().reduceOne(m.selected());  // hand over the goods
        m.bankBalance();                         // wait — see note below
        if (change > 0) {
            // bankBalance() banked everything; give the change back out of the box.
            std::cout << "Change: " << change << " -> " << makeChange(change) << "\n";
        }
        m.setSelected("");
        m.setState(m.idle());                    // Dispensing -> Idle
        return p;
    }
    void cancel(VendingMachine& m) override {
        std::cout << "Refunded " << m.refundBalance() << "\n";
        m.setSelected("");
        m.setState(m.idle());
    }
    std::string name() const override { return "Dispensing"; }
private:
    static std::string makeChange(int amount);   // Step 7
};
```

> Small bookkeeping refinement: bank exactly the `price` and refund the rest. Cleaner version:
> ```cpp
> int change = m.balance() - p.price;
> m.inventory().reduceOne(m.selected());
> // bank only the price, return the change to the user
> m.addBalance(-change);   // balance now == price
> m.bankBalance();         // cashBox += price; balance = 0
> if (change > 0) std::cout << "Change: " << makeChange(change) << "\n";
> ```

> **Thinking habit:** write the four methods as a 4-row truth table per state — "valid? → do work + transition; invalid? → throw." Fill every cell; no gaps = no undefined behaviour.

---

## Step 6b — Wire the Context constructor

Now that the states exist, build them once and start in `Idle`:

```cpp
VendingMachine::VendingMachine() {
    idle_       = std::make_unique<IdleState>();
    hasMoney_   = std::make_unique<HasMoneyState>();
    dispensing_ = std::make_unique<DispensingState>();
    state_      = idle_.get();             // start Idle
}
State* VendingMachine::idle()       { return idle_.get(); }
State* VendingMachine::hasMoney()   { return hasMoney_.get(); }
State* VendingMachine::dispensing() { return dispensing_.get(); }
```

The machine **owns** the three states via `unique_ptr`; `state_` is a non-owning view into them. Because states are data-free, three instances cover every machine forever.

> **Thinking habit:** own long-lived objects in one place (`unique_ptr` members), pass around raw/non-owning pointers for "who's current." Ownership vs. usage — keep them separate.

---

## Step 7 — Follow-up: change-making (the sneaky little algorithm)

Return change from largest coin down — **greedy**. Works for "canonical" coin systems like {1,5,10,20}; because a `1` exists, exact change is always possible.

```cpp
std::string DispensingState::makeChange(int amount) {
    const int coins[] = {20, 10, 5, 1};
    std::string out;
    for (int c : coins) {
        int n = amount / c;
        if (n > 0) { out += std::to_string(n) + "x" + std::to_string(c) + " "; amount -= n * c; }
    }
    return out;  // amount == 0 here for {1,5,10,20}
}
```

What an interviewer pushes on next:
- **"Limited coins of each denom?"** Greedy can fail → track per-denom counts; if you run dry, fall back to DP / report **"cannot make exact change"** and refund instead of vending.
- **"Non-canonical denominations"** (e.g. {1,3,4} making 6) → greedy is wrong; use DP (coin-change min-coins).

> **Thinking habit:** say out loud *why* greedy is safe here (canonical set + a `1` coin). Naming the assumption is what separates a real answer from a lucky one.

---

## Step 8 — Prove it with a driver

```cpp
int main() {
    VendingMachine m;
    m.refill("A1", 2);                       // stock 2 units in slot A1
    // (in a fuller model refill would carry name/price; kept short here)

    m.selectProduct("A1");                   // Idle -> HasMoney
    m.insertCoin(Coin::TEN);
    m.insertCoin(Coin::TEN);                 // balance 20 >= price -> Dispensing
    Product p = m.dispense();                // drops product, returns change, -> Idle
    std::cout << "Got: " << p.code << "\n";

    // Illegal op in Idle is rejected, not crashed:
    try { m.dispense(); }
    catch (const std::exception& e) { std::cout << "Rejected: " << e.what() << "\n"; }
}
```

> **Thinking habit:** the driver must hit a full happy cycle (`Idle→HasMoney→Dispensing→Idle`) *and* one illegal op. That proves both the transitions and the rejection logic.

---

## Step 9 — Talk through the remaining follow-ups

1. **Concurrent users on the same slot.** Two buyers race for the last unit. Guard the `hasStock → reduceOne` step with a lock (or compare-and-swap on qty). The State pattern is untouched; you're adding a mutex around the inventory mutation.
2. **Multiple payment methods (card/UPI).** Introduce a `PaymentStrategy` interface (`authorize(amount)`), inject it into the machine. `insertCoin` becomes one implementation; card/UPI are others. **Strategy pattern** sitting beside the State pattern — payment *how* is orthogonal to machine *mode*.
3. **More states** (e.g. `OutOfServiceState`, `MaintenanceState`). Just add another `State` subclass — no existing code changes. That extensibility is the payoff of the pattern.

> **Thinking habit:** end by showing a new requirement = a new subclass, not an edit to a switch. That's the open/closed principle, and it's what the interviewer wants to hear.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — "state objects, not if/switch" literally names the pattern.
2. **Three roles**: Context (`VendingMachine`), State interface, concrete states.
3. **Interface first** — Context's public methods are *pure delegation* one-liners.
4. **Leaves first** (`Coin`, `Product`, `Inventory`), give `Coin` a real value.
5. **Context holds data + driver-helpers**; states are **stateless** and own their **transitions**.
6. **Per state, a 4-row truth table** — valid ops do work + transition, illegal ops throw.
7. **Own states once** (`unique_ptr`), pass a non-owning `state_` for "current."
8. **Follow-ups = new subclasses / new strategy**, never a new `switch`.

Follow that skeleton on any "machine with modes" LLD (ATM, elevator, order lifecycle, traffic light) and the State pattern falls out almost mechanically.
