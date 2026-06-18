# Vending Machine — Complete Single-File Solution

This document is the **paste-ready, one-file** version of the [step-by-step reasoning](sol.md). Everything — `Coin`, `Product`, `Inventory`, the `State` interface, the three concrete states, the `VendingMachine` context, change-making, and a driver `main()` — lives in a single `.cpp` you can compile and run as-is.

> Read [sol.md](sol.md) for *why* the design looks like this (the State-pattern reasoning). This file is the *what* — the finished code in one place.

---

## How to compile & run

```bash
g++ -std=c++17 -Wall -Wextra -o vending vending.cpp
./vending
```

---

## The ordering problem (why one file needs care)

In a single translation unit you can't define everything top-to-bottom naively, because the pieces refer to each other in a cycle:

- The **states** call methods on `VendingMachine` (`setState`, `addBalance`, …) → they need `VendingMachine` *declared*.
- `VendingMachine` holds `unique_ptr<State>` members and its constructor builds the concrete states → it needs `State` *defined* and the concrete states *defined*.

The clean resolution for one file:

1. **Leaves first** — `Coin`, `Product`, `Inventory` (no dependencies).
2. **Forward-declare** `class VendingMachine;`
3. Define the **`State` interface** (only needs the forward declaration — all its methods take `VendingMachine&`).
4. Define **`VendingMachine`** fully, but **declare** (don't define) the methods whose bodies call into states — i.e. the constructor and the delegating one-liners can be inline (they only need `State` declared), but anything is fine since `State` is fully defined by now.
5. Define the **three concrete states** (they need the full `VendingMachine` definition, which now exists).
6. Define `VendingMachine`'s **constructor out-of-line**, after the concrete states exist (it does `make_unique<IdleState>()` etc.).
7. `main()`.

That single ordering decision is the only thing the "one file" constraint actually forces on you.

---

## The complete file

```cpp
// vending.cpp — Vending Machine LLD, State pattern, single file.
// Build: g++ -std=c++17 -Wall -Wextra -o vending vending.cpp && ./vending

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

// ============================================================
// Step 3 — Leaves: Coin, Product, Inventory (no dependencies)
// ============================================================

// Give each coin its real integer value so change-making is a static_cast.
enum class Coin { ONE = 1, FIVE = 5, TEN = 10, TWENTY = 20 };
inline int valueOf(Coin c) { return static_cast<int>(c); }

struct Product {
    std::string code;
    std::string name;
    int         price = 0;   // same units as coins
};

// Hides "code -> (product, qty)". The machine asks questions; it never
// sees the raw map (encapsulation).
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
    struct Entry { Product product; int qty = 0; };
    std::unordered_map<std::string, Entry> entries_;
};

// ============================================================
// Step 4 — State interface (needs only a forward declaration)
// ============================================================

class VendingMachine;   // forward declaration — states reference it

class State {
public:
    virtual ~State() = default;
    virtual void        selectProduct(VendingMachine&, const std::string&) = 0;
    virtual void        insertCoin(VendingMachine&, Coin)                  = 0;
    virtual Product     dispense(VendingMachine&)                          = 0;
    virtual void        cancel(VendingMachine&)                            = 0;
    virtual std::string name() const                                       = 0;
};

// ============================================================
// Step 5 — Context: VendingMachine (data + helpers states drive)
// ============================================================

class VendingMachine {
public:
    VendingMachine();   // defined out-of-line, after the states exist

    // ---- public user API: pure delegation, zero logic ----
    void    selectProduct(const std::string& code) { state_->selectProduct(*this, code); }
    void    insertCoin(Coin c)                      { state_->insertCoin(*this, c); }
    Product dispense()                              { return state_->dispense(*this); }
    void    cancel()                                { state_->cancel(*this); }

    // ---- admin ----
    void refill(const Product& p, int qty) { inventory_.add(p, qty); }
    int  collectCash() { int c = cashBox_; cashBox_ = 0; return c; }

    // ---- helpers the STATES use to drive the machine ----
    void   setState(State* s) { state_ = s; }
    State* idle()       { return idle_.get(); }
    State* hasMoney()   { return hasMoney_.get(); }
    State* dispensing() { return dispensing_.get(); }

    Inventory& inventory() { return inventory_; }
    int   balance() const  { return balance_; }
    void  addBalance(int v) { balance_ += v; }
    int   refundBalance() { int b = balance_; balance_ = 0; return b; }
    void  bankBalance()   { cashBox_ += balance_; balance_ = 0; }
    const std::string& selected() const        { return selectedCode_; }
    void  setSelected(const std::string& code) { selectedCode_ = code; }

private:
    std::unique_ptr<State> idle_, hasMoney_, dispensing_;  // machine OWNS the states
    State*      state_ = nullptr;                          // current (non-owning view)
    Inventory   inventory_;
    int         balance_ = 0;   // coins inserted this session
    int         cashBox_ = 0;   // accumulated takings (admin collects)
    std::string selectedCode_;
};

// ============================================================
// Step 7 — Change-making (greedy; safe for canonical {1,5,10,20})
// ============================================================

static std::string makeChange(int amount) {
    const int coins[] = {20, 10, 5, 1};
    std::string out;
    for (int c : coins) {
        int n = amount / c;
        if (n > 0) {
            out += std::to_string(n) + "x" + std::to_string(c) + " ";
            amount -= n * c;
        }
    }
    return out;  // amount == 0 here because a '1' coin exists
}

// ============================================================
// Step 6 — Concrete states (each rejects what it can't do)
// ============================================================

// ---------- Idle: nothing selected, no money ----------
class IdleState : public State {
public:
    void selectProduct(VendingMachine& m, const std::string& code) override {
        if (!m.inventory().hasStock(code))
            throw std::invalid_argument("out of stock: " + code);
        m.setSelected(code);
        m.setState(m.hasMoney());            // Idle -> HasMoney
    }
    void insertCoin(VendingMachine&, Coin) override {
        throw std::logic_error("select a product first");
    }
    Product dispense(VendingMachine&) override {
        throw std::logic_error("nothing selected");
    }
    void cancel(VendingMachine&) override { /* nothing to refund */ }
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
            m.setState(m.dispensing());      // enough money -> ready to vend
    }
    Product dispense(VendingMachine&) override {
        throw std::logic_error("insufficient funds");
    }
    void cancel(VendingMachine& m) override {
        std::cout << "Refunded " << m.refundBalance() << "\n";
        m.setSelected("");
        m.setState(m.idle());                // HasMoney -> Idle
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

        m.inventory().reduceOne(m.selected());   // hand over the goods
        m.addBalance(-change);                    // balance now == price
        m.bankBalance();                          // cashBox += price; balance = 0
        if (change > 0)
            std::cout << "Change: " << change << " -> " << makeChange(change) << "\n";

        Product out = p;                          // copy before we clear selection
        m.setSelected("");
        m.setState(m.idle());                     // Dispensing -> Idle
        return out;
    }
    void cancel(VendingMachine& m) override {
        std::cout << "Refunded " << m.refundBalance() << "\n";
        m.setSelected("");
        m.setState(m.idle());
    }
    std::string name() const override { return "Dispensing"; }
};

// ============================================================
// Step 6b — Context constructor (after states are defined)
// ============================================================

VendingMachine::VendingMachine() {
    idle_       = std::make_unique<IdleState>();
    hasMoney_   = std::make_unique<HasMoneyState>();
    dispensing_ = std::make_unique<DispensingState>();
    state_      = idle_.get();                // start Idle
}

// ============================================================
// Step 8 — Driver: a full happy cycle + an illegal op
// ============================================================

int main() {
    VendingMachine m;
    m.refill({"A1", "Cola", 15}, 2);          // stock 2 units of a 15-priced product

    // ---- happy path: Idle -> HasMoney -> Dispensing -> Idle ----
    m.selectProduct("A1");                     // Idle -> HasMoney
    m.insertCoin(Coin::TEN);                   // balance 10  (< 15)
    m.insertCoin(Coin::TEN);                   // balance 20  (>= 15) -> Dispensing
    Product p = m.dispense();                  // drops product, returns 5 change -> Idle
    std::cout << "Got: " << p.name << " (" << p.code << ")\n";

    // ---- cancel mid-session refunds and returns to Idle ----
    m.selectProduct("A1");
    m.insertCoin(Coin::FIVE);
    m.cancel();                                // "Refunded 5", back to Idle

    // ---- illegal op in Idle is rejected, not crashed ----
    try { m.dispense(); }
    catch (const std::exception& e) { std::cout << "Rejected: " << e.what() << "\n"; }

    // ---- out-of-stock rejection ----
    m.selectProduct("A1"); m.insertCoin(Coin::TWENTY); m.dispense();  // last unit
    try { m.selectProduct("A1"); }
    catch (const std::exception& e) { std::cout << "Rejected: " << e.what() << "\n"; }

    std::cout << "Cash collected by admin: " << m.collectCash() << "\n";
    return 0;
}
```

---

## Expected output

```
Change: 5 -> 1x5
Got: Cola (A1)
Refunded 5
Rejected: nothing selected
Change: 5 -> 1x5
Rejected: out of stock: A1
Cash collected by admin: 30
```

> You see: a change line + the dispensed product (first sale), a refund (cancel), an illegal-op rejection (`dispense` in Idle), a second change line (second sale of the last unit, 20 − 15 = 5), an out-of-stock rejection, and the banked cash (15 + 15 = 30 from the two sales).

*(Verified: compiles with `g++ -std=c++17 -Wall -Wextra` — no warnings — and produces exactly the output above.)*

---

## What changed vs. the multi-step `sol.md`

The reasoning doc kept a couple of deliberate rough edges to teach a point; the single file tidies them so it actually compiles and runs:

| In `sol.md` (teaching) | Here (production single file) |
|---|---|
| `refill(code, qty)` created a price-0 product | `refill(Product, qty)` carries name + price |
| `makeChange` was a `static` member of `DispensingState` | free function above the states — one definition, callable anywhere |
| dispense banked everything then "noted" change | banks exactly `price`, refunds the rest (`addBalance(-change); bankBalance();`) |

Everything else maps 1:1 to the steps in [sol.md](sol.md).

---

## Reusable skeleton (one file, any "machine with modes")

1. **Leaves first** — value types with no dependencies.
2. **Forward-declare the Context**, then define the **State interface** (methods take `Context&`).
3. **Define the Context** with delegating one-liner public methods + driver helpers; **own states via `unique_ptr`**, keep a non-owning `state_`.
4. **Define concrete states** (now the full Context exists) — each a truth table: valid op → work + transition, illegal op → throw.
5. **Define the Context constructor out-of-line** (it builds the concrete states).
6. **`main()`** drives a full transition cycle plus one rejection.

Same five-move ordering works for ATM, elevator, order lifecycle, or traffic light in a single file.
```
