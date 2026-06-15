# Vending Machine — LLD Problem Statement

**Difficulty:** Easy
**Language:** C++
**Pattern focus:** State pattern (the canonical State-pattern problem)

---

## Context
Design the controller for a vending machine that holds products in slots and accepts coins/notes.

## Functional Requirements
- The machine moves through states: `Idle → HasMoney → Dispensing → Idle`.
- A user **selects a product**, **inserts money incrementally**, and the machine **dispenses** the product and **returns change** — or **refunds** if cancelled.
- Reject a selection if the product is **out of stock** or if inserted money is **insufficient**.
- An **admin** can refill inventory and collect accumulated cash.

## Non-Functional / Constraints
- Behaviour must be driven by **state objects**, not a giant `if/switch` on an enum.
- Each state defines its own response to `insertCoin`, `selectProduct`, `dispense`, `cancel`.

## Expected Public Interface
```cpp
enum class Coin { ONE, FIVE, TEN, TWENTY };

class VendingMachine {
public:
    void selectProduct(const std::string& code);
    void insertCoin(Coin c);
    Product dispense();        // valid only in Dispensing state
    void cancel();             // refund all inserted money, return to Idle

    // admin
    void refill(const std::string& code, int qty);
    int  collectCash();
};

class State {  // base class for IdleState, HasMoneyState, DispensingState
public:
    virtual void selectProduct(VendingMachine&, const std::string&) = 0;
    virtual void insertCoin(VendingMachine&, Coin) = 0;
    virtual Product dispense(VendingMachine&) = 0;
    virtual void cancel(VendingMachine&) = 0;
    virtual ~State() = default;
};
```

## What the Interviewer Is Really Testing
- A proper **State pattern**: concrete state classes, transitions owned by the states themselves.
- That illegal operations in a given state are handled by that state (e.g. `dispense()` while `Idle`).

## Follow-Up Questions to Expect
1. **Change-making**: return optimal change from available denominations (sneaks in a small greedy/DP algorithm; handle "cannot make exact change").
2. **Concurrent users** competing for the same slot.
3. **Multiple payment methods** (card, UPI) behind a payment abstraction.

## Your Task
1. Model the three states as classes implementing the `State` interface.
2. Make the machine delegate every operation to its current state.
3. Then attempt the change-making follow-up.
