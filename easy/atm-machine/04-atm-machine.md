# ATM Machine — LLD Problem Statement

**Difficulty:** Easy
**Language:** C++
**Pattern focus:** State pattern (session flow) + Chain of Responsibility (cash dispensing)

---

## Context
Design the control flow of an ATM — from card insertion through cash withdrawal.

## Functional Requirements
- Session states: `Idle → CardInserted → PinAuthenticated → SelectOperation → TransactionComplete → Idle`.
- Operations: **check balance**, **withdraw cash**, **deposit**. The user can **eject card / cancel** at (almost) any point.
- A withdrawal validates against **both** the account balance **and** the machine's available cash.
- Dispense the requested amount using available denominations — prefer **fewer notes** (e.g. ₹2000 first, then ₹500, then ₹100).

## Non-Functional / Constraints
- The session flow should be a **State pattern**, not nested `if`s.
- The dispenser should be a **Chain of Responsibility**: a ₹2000 handler → ₹500 handler → ₹100 handler, each dispensing what it can and passing the remainder down the chain.

## Expected Public Interface
```cpp
enum class OpType { CHECK_BALANCE, WITHDRAW, DEPOSIT };

class ATM {
public:
    void insertCard(const Card& c);
    void enterPin(const std::string& pin);
    void selectOperation(OpType op);
    void withdraw(int amount);
    void ejectCard();
};

class CashDispenseHandler {       // Chain of Responsibility node
public:
    void setNext(CashDispenseHandler* next);
    virtual void dispense(int amount) = 0;   // Note2000Handler, Note500Handler, Note100Handler
    virtual ~CashDispenseHandler() = default;
protected:
    CashDispenseHandler* next_ = nullptr;
};
```

## What the Interviewer Is Really Testing
- A clean **State pattern** for the session.
- The **Chain of Responsibility** dispenser — this is the detail that separates a strong answer from a basic one.
- Correct handling of "balance OK but machine lacks the right denominations."

## Follow-Up Questions to Expect
1. The amount cannot be made from available denominations — reject the whole transaction (no partial dispense).
2. **Concurrent access** to the shared cash store (multiple ATMs against one account, or thread-safe dispenser).
3. Transaction **logging and rollback** on a dispense failure mid-way.

## Your Task
1. Model the session states as classes.
2. Build the dispenser chain; ensure it validates feasibility before dispensing.
3. Attempt the rollback follow-up.
