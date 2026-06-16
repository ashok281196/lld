# ATM Machine — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes **two patterns** — State (session flow) and Chain of Responsibility (cash dispensing) — feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"The session flow should be a **State pattern**, not nested `if`s."* → The card→pin→operation→done lifecycle is a state machine. A `switch (sessionState)` answer is a fail. Same shape as the vending machine: one **Context** (the ATM) + a **State interface** + concrete states.
2. *"The dispenser should be a **Chain of Responsibility**: ₹2000 → ₹500 → ₹100, each dispensing what it can and passing the remainder down."* → This is the **second** pattern, and the detail that separates a strong answer from a basic one. Don't fold dispensing into the ATM with a loop — build the chain.
3. *"A withdrawal validates against **both** the account balance **and** the machine's available cash."* → Two independent guards before any money moves. Order matters: check, then dispense.
4. *"Prefer **fewer notes**."* → Greedy, largest-denomination-first. The chain order *is* the greedy strategy.
5. Follow-ups name the seams: all-or-nothing dispense (feasibility before mutation), concurrency on the shared cash store, logging + rollback.

> **Thinking habit:** when a prompt names *two* patterns, it's telling you the system splits along a seam. Find the seam (here: "session control" vs. "cash mechanics") and let each pattern own one side.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *ATM, card, account, PIN, session, state, operation, withdrawal, deposit, cash, denomination, note, dispenser, handler.*

Split them along the seam from Step 0 — session control vs. cash mechanics:

| Class | Owns | Why it exists |
|-------|------|---------------|
| `Card` | card number → account id | what the user inserts |
| `Account` | id, pin, balance | the money behind the card |
| `Bank` | accounts | authenticates, debits/credits |
| `CashInventory` | note counts per denomination | the machine's physical cash |
| `ATMState` (interface) | the 5 session operations | one reaction-set per session mode |
| `IdleState` / `CardInsertedState` / `PinAuthenticatedState` / `SelectOperationState` | transition rules for that mode | the session behaviours |
| `ATM` (the **Context**) | current state + bank + cash + active card | delegates every call to its state |
| `CashDispenseHandler` (interface) | one denomination's dispensing | a node in the Chain of Responsibility |
| `Note2000Handler` / `Note500Handler` / `Note100Handler` | dispense-what-I-can, pass remainder | the actual chain links |

> **Thinking habit:** two patterns ⇒ two mini class-hierarchies. The State family (`ATMState` + concretes) and the Chain family (`CashDispenseHandler` + concretes) are independent — they only meet inside `ATM`.

---

## Step 2 — Pin the public interface (the contract)

Given to us — lock it before internals:

```cpp
enum class OpType { CHECK_BALANCE, WITHDRAW, DEPOSIT };

class ATM {
public:
    void insertCard(const Card& c);
    void enterPin(const std::string& pin);
    void selectOperation(OpType op);
    void withdraw(int amount);
    void ejectCard();          // cancel at (almost) any point -> Idle
};
```

As in the vending machine, the realization is that **none of these methods contain session logic** — each forwards to the current state:

```cpp
void enterPin(const std::string& pin) { state_->enterPin(*this, pin); }
```

The dispenser has its own tiny contract — a chain node that knows its successor:

```cpp
class CashDispenseHandler {
public:
    void setNext(CashDispenseHandler* next) { next_ = next; }
    virtual void dispense(int amount) = 0;     // Note2000/500/100 handlers
    virtual ~CashDispenseHandler() = default;
protected:
    CashDispenseHandler* next_ = nullptr;
};
```

> **Thinking habit:** two contracts, two responsibilities. `ATM`'s methods delegate session flow; `CashDispenseHandler::dispense` is recursive-by-composition. Don't let one leak into the other.

---

## Step 3 — Model the leaves: `Card`, `Account`, `Bank`, `CashInventory`

Bottom-up: dependency-free types first.

```cpp
struct Card {
    std::string cardNumber;
    int accountId = 0;
};

struct Account {
    int         id = 0;
    std::string pin;
    int         balance = 0;
};
```

`Bank` hides the account map — the ATM asks it questions, never touches accounts directly (encapsulation — same rule as the vending machine's `Inventory`).

```cpp
class Bank {
public:
    void addAccount(const Account& a) { accounts_[a.id] = a; }

    bool authenticate(int accountId, const std::string& pin) const {
        auto it = accounts_.find(accountId);
        return it != accounts_.end() && it->second.pin == pin;
    }
    int  balance(int accountId) const { return accounts_.at(accountId).balance; }
    void debit (int accountId, int amount) { accounts_.at(accountId).balance -= amount; }
    void credit(int accountId, int amount) { accounts_.at(accountId).balance += amount; }

private:
    std::unordered_map<int, Account> accounts_;
};
```

`CashInventory` tracks how many notes of each denomination the machine holds. Give the denominations real integer values now — the chain reads them directly.

```cpp
class CashInventory {
public:
    void load(int denom, int count) { notes_[denom] += count; }
    int  count(int denom) const {
        auto it = notes_.find(denom);
        return it == notes_.end() ? 0 : it->second;
    }
    void remove(int denom, int count) { notes_.at(denom) -= count; }

    // total currency physically available
    int totalCash() const {
        int sum = 0;
        for (auto& [denom, cnt] : notes_) sum += denom * cnt;
        return sum;
    }

private:
    std::unordered_map<int, int> notes_;   // denomination -> note count
};
```

> **Thinking habit:** model the *physical* resource (`CashInventory`) separately from the *logical* one (`Account` balance). The withdrawal must satisfy both — keeping them apart makes "balance OK but no notes" expressible.

---

## Step 4 — The key insight: Chain of Responsibility for dispensing

This is the differentiator. Spend real thought here.

**Naive idea:** loop over denominations largest-first, subtract notes, stop at zero. It works, but it bakes the denomination list and the greedy logic into one function. The problem explicitly asks for a *chain* — and the chain buys you **open/closed extensibility** (add a ₹200 note = insert one link, change nothing else).

**The chain shape.** Each handler owns *one* denomination. On `dispense(amount)`:

1. Compute how many of *my* notes I can use: `min(amount / myDenom, notesAvailable)`.
2. Hand those out (decrement inventory), subtract from `amount`.
3. Pass the **remainder** to `next_` (if any). The tail link receiving a non-zero remainder means we couldn't complete — but we guard against that *before* we ever start (Step 5).

```cpp
class Note2000Handler : public CashDispenseHandler {
public:
    explicit Note2000Handler(CashInventory& inv) : inv_(inv) {}
    void dispense(int amount) override {
        const int denom = 2000;
        int need  = amount / denom;
        int have  = inv_.count(denom);
        int give  = std::min(need, have);

        if (give > 0) {
            inv_.remove(denom, give);
            std::cout << "Dispensing " << give << " x " << denom << "\n";
            amount -= give * denom;
        }
        if (amount > 0 && next_) next_->dispense(amount);   // pass remainder down
    }
private:
    CashInventory& inv_;
};
```

`Note500Handler` and `Note100Handler` are identical except for `denom` (in a real codebase you'd template or parameterize the denom; spelled out here for clarity). Wiring the chain is just `setNext`:

```cpp
Note2000Handler h2000(cash);
Note500Handler  h500(cash);
Note100Handler  h100(cash);
h2000.setNext(&h500);
h500.setNext(&h100);
// h2000 is the chain head; dispensing always starts there.
```

Why the chain order *is* the greedy "fewer notes" rule: the largest denomination gets first pick of the amount, so it consumes as much as it can before passing the leftover down. Largest-first = fewest notes for a canonical set.

> ⚠️ **Dispense is destructive — never start it without proving feasibility first.** A chain that hands out ₹2000 then discovers it can't make the last ₹100 has already emptied real cash. The all-or-nothing requirement (follow-up 1) means feasibility is computed *before* the first `remove`. See Step 5.

> **Thinking habit:** Chain of Responsibility = each link does its bit and forwards the rest. Reach for it when a request is satisfied by a *sequence of partial handlers* — dispensing, logging filters, approval tiers.

---

## Step 5 — Feasibility before mutation: can we make this amount?

Before the chain mutates anything, simulate it on *copies* of the counts. If the simulation can't reach zero, reject the whole withdrawal — no partial dispense, no rollback needed.

```cpp
// Greedy feasibility on a snapshot. Returns true iff `amount` is fully makeable.
bool canDispense(const CashInventory& inv, int amount) {
    const int denoms[] = {2000, 500, 100};
    int remaining = amount;
    for (int d : denoms) {
        int give = std::min(remaining / d, inv.count(d));
        remaining -= give * d;
    }
    return remaining == 0;
}
```

Now a withdrawal has a clean three-guard sequence — and *only* after all three pass does any state change:

1. `amount > 0` and a multiple of the smallest note.
2. account `balance >= amount` (logical funds).
3. `canDispense(cash, amount)` (physical notes).

> **Thinking habit:** for any destructive operation, *check feasibility on a snapshot, mutate only after all guards pass.* "Validate → mutate" — the same rhythm as the game's `makeMove`, now protecting real cash.

---

## Step 6 — The Context: `ATM` holds data + helpers the states drive

The ATM exposes *just enough* for states to do their job: authenticate, read/debit balance, run the dispenser, switch state, remember the active card. These helpers are the machine's "internal API for its states."

```cpp
class ATMState;   // forward declaration — states reference the ATM

class ATM {
public:
    ATM(Bank& bank, CashInventory& cash);   // defined after states exist (Step 8)

    // ---- public user API: pure delegation ----
    void insertCard(const Card& c)            { state_->insertCard(*this, c); }
    void enterPin(const std::string& pin)     { state_->enterPin(*this, pin); }
    void selectOperation(OpType op)           { state_->selectOperation(*this, op); }
    void withdraw(int amount)                 { state_->withdraw(*this, amount); }
    void ejectCard()                          { state_->ejectCard(*this); }

    // ---- helpers the STATES use to drive the machine ----
    void setState(ATMState* s) { state_ = s; }
    ATMState* idle();          // accessors to the four state singletons
    ATMState* cardInserted();
    ATMState* pinAuthenticated();
    ATMState* selectOperation();

    Bank&          bank()  { return bank_; }
    CashInventory& cash()  { return cash_; }

    void setCard(const Card& c) { activeCard_ = c; }
    const Card& card() const    { return activeCard_; }
    void clearSession() { activeCard_ = {}; selectedOp_ = OpType::CHECK_BALANCE; }

    void   setOp(OpType op) { selectedOp_ = op; }
    OpType op() const       { return selectedOp_; }

private:
    std::unique_ptr<ATMState> idle_, cardInserted_, pinAuthed_, selectOp_;  // owns states
    ATMState* state_ = nullptr;        // current (non-owning view)
    Bank&          bank_;              // shared — see concurrency follow-up
    CashInventory& cash_;
    Card   activeCard_{};
    OpType selectedOp_ = OpType::CHECK_BALANCE;
};
```

> **Thinking habit:** the Context owns the *session data*; the shared resources (`Bank`, `CashInventory`) are held by reference because they outlive a single ATM and may be shared across machines. Ownership vs. reference is a design statement.

---

## Step 7 — The State interface + the four states (each rejects what it can't do)

The base interface — five operations, one per public ATM call:

```cpp
class ATMState {
public:
    virtual ~ATMState() = default;
    virtual void insertCard(ATM&, const Card&)        = 0;
    virtual void enterPin(ATM&, const std::string&)   = 0;
    virtual void selectOperation(ATM&, OpType)        = 0;
    virtual void withdraw(ATM&, int amount)           = 0;
    virtual void ejectCard(ATM&)                      = 0;
    virtual std::string name() const                  = 0;
};
```

Each state answers all five — valid ops do work + transition, invalid ops reject cleanly. This is where "session flow, not nested `if`s" lives.

```cpp
// ---------- Idle: waiting for a card ----------
class IdleState : public ATMState {
public:
    void insertCard(ATM& atm, const Card& c) override {
        atm.setCard(c);
        atm.setState(atm.cardInserted());          // Idle -> CardInserted
    }
    void enterPin(ATM&, const std::string&) override { throw std::logic_error("insert card first"); }
    void selectOperation(ATM&, OpType) override     { throw std::logic_error("insert card first"); }
    void withdraw(ATM&, int) override               { throw std::logic_error("insert card first"); }
    void ejectCard(ATM&) override                   { /* nothing to eject */ }
    std::string name() const override { return "Idle"; }
};

// ---------- CardInserted: have a card, awaiting PIN ----------
class CardInsertedState : public ATMState {
public:
    void insertCard(ATM&, const Card&) override { throw std::logic_error("card already inserted"); }
    void enterPin(ATM& atm, const std::string& pin) override {
        if (!atm.bank().authenticate(atm.card().accountId, pin))
            throw std::invalid_argument("invalid PIN");
        atm.setState(atm.pinAuthenticated());      // CardInserted -> PinAuthenticated
    }
    void selectOperation(ATM&, OpType) override { throw std::logic_error("authenticate first"); }
    void withdraw(ATM&, int) override           { throw std::logic_error("authenticate first"); }
    void ejectCard(ATM& atm) override { atm.clearSession(); atm.setState(atm.idle()); }
    std::string name() const override { return "CardInserted"; }
};

// ---------- PinAuthenticated: pick an operation ----------
class PinAuthenticatedState : public ATMState {
public:
    void insertCard(ATM&, const Card&) override { throw std::logic_error("session in progress"); }
    void enterPin(ATM&, const std::string&) override { throw std::logic_error("already authenticated"); }
    void selectOperation(ATM& atm, OpType op) override {
        atm.setOp(op);
        if (op == OpType::CHECK_BALANCE) {
            std::cout << "Balance: " << atm.bank().balance(atm.card().accountId) << "\n";
            atm.clearSession();
            atm.setState(atm.idle());              // one-shot op, back to Idle
        } else {
            atm.setState(atm.selectOperation());   // WITHDRAW/DEPOSIT need an amount
        }
    }
    void withdraw(ATM&, int) override { throw std::logic_error("select WITHDRAW first"); }
    void ejectCard(ATM& atm) override { atm.clearSession(); atm.setState(atm.idle()); }
    std::string name() const override { return "PinAuthenticated"; }
};

// ---------- SelectOperation: operation chosen, awaiting amount ----------
class SelectOperationState : public ATMState {
public:
    void insertCard(ATM&, const Card&) override { throw std::logic_error("session in progress"); }
    void enterPin(ATM&, const std::string&) override { throw std::logic_error("already authenticated"); }
    void selectOperation(ATM&, OpType) override { throw std::logic_error("operation already selected"); }

    void withdraw(ATM& atm, int amount) override {
        if (atm.op() != OpType::WITHDRAW) throw std::logic_error("WITHDRAW not selected");

        int accountId = atm.card().accountId;
        // ---- the three guards (Step 5) — all pass before any mutation ----
        if (amount <= 0 || amount % 100 != 0)
            throw std::invalid_argument("amount must be a positive multiple of 100");
        if (atm.bank().balance(accountId) < amount)
            throw std::invalid_argument("insufficient account balance");
        if (!canDispense(atm.cash(), amount))
            throw std::invalid_argument("cannot dispense this amount with available notes");

        // ---- mutate: debit then run the dispenser chain ----
        atm.bank().debit(accountId, amount);
        atm.dispenserHead()->dispense(amount);     // chain handed to ATM at construction

        atm.clearSession();
        atm.setState(atm.idle());                  // TransactionComplete -> Idle
    }
    void ejectCard(ATM& atm) override { atm.clearSession(); atm.setState(atm.idle()); }
    std::string name() const override { return "SelectOperation"; }
};
```

> **Thinking habit:** write each state as a 5-row truth table — "valid? → work + transition; invalid? → throw." `ejectCard` is valid almost everywhere (that's the "cancel at any point" requirement); fill every other cell so there's no undefined behaviour.

---

## Step 8 — Wire the chain into the Context, then build the states

The ATM needs a handle to the chain head so a state can call `dispense`. Hold the handlers as owned members and expose the head:

```cpp
// add to ATM:
//   std::unique_ptr<CashDispenseHandler> h2000_, h500_, h100_;
//   CashDispenseHandler* dispenserHead_ = nullptr;
//   public: CashDispenseHandler* dispenserHead() { return dispenserHead_; }

ATM::ATM(Bank& bank, CashInventory& cash) : bank_(bank), cash_(cash) {
    // --- build the State family ---
    idle_         = std::make_unique<IdleState>();
    cardInserted_ = std::make_unique<CardInsertedState>();
    pinAuthed_    = std::make_unique<PinAuthenticatedState>();
    selectOp_     = std::make_unique<SelectOperationState>();
    state_        = idle_.get();                 // start Idle

    // --- build + wire the Chain family ---
    h2000_ = std::make_unique<Note2000Handler>(cash_);
    h500_  = std::make_unique<Note500Handler>(cash_);
    h100_  = std::make_unique<Note100Handler>(cash_);
    h2000_->setNext(h500_.get());
    h500_->setNext(h100_.get());
    dispenserHead_ = h2000_.get();               // largest-first = fewest notes
}
ATMState* ATM::idle()              { return idle_.get(); }
ATMState* ATM::cardInserted()      { return cardInserted_.get(); }
ATMState* ATM::pinAuthenticated()  { return pinAuthed_.get(); }
ATMState* ATM::selectOperation()   { return selectOp_.get(); }
```

The ATM **owns** both families via `unique_ptr`; `state_` and `dispenserHead_` are non-owning views. Because states are data-free, four instances cover every session forever.

> **Thinking habit:** own long-lived objects once (`unique_ptr` members); pass raw, non-owning pointers for "who's current" / "chain head." Build *and wire* the chain in the constructor so the head is the only entry point.

---

## Step 9 — Prove it with a driver

Hit a full happy cycle, a denomination-shortage rejection, and an illegal op.

```cpp
int main() {
    Bank bank;
    bank.addAccount({1001, "1234", 5000});      // id, pin, balance

    CashInventory cash;
    cash.load(2000, 2);                          // 2 x 2000
    cash.load(500,  3);                          // 3 x 500
    cash.load(100,  5);                          // 5 x 100

    ATM atm(bank, cash);

    // ---- happy path: Idle -> ... -> withdraw 2600 -> Idle ----
    atm.insertCard({"XYZ", 1001});               // Idle -> CardInserted
    atm.enterPin("1234");                        // -> PinAuthenticated
    atm.selectOperation(OpType::WITHDRAW);       // -> SelectOperation
    atm.withdraw(2600);                          // 1x2000 + 1x500 + 1x100, debits 2600 -> Idle

    // ---- rejection: balance OK but notes can't form the amount ----
    atm.insertCard({"XYZ", 1001});
    atm.enterPin("1234");
    atm.selectOperation(OpType::WITHDRAW);
    try { atm.withdraw(50); }                     // not a multiple of 100
    catch (const std::exception& e) { std::cout << "Rejected: " << e.what() << "\n"; }
    atm.ejectCard();                              // cancel mid-session -> Idle

    // ---- illegal op in Idle is rejected, not crashed ----
    try { atm.withdraw(100); }
    catch (const std::exception& e) { std::cout << "Rejected: " << e.what() << "\n"; }
    return 0;
}
```

> **Thinking habit:** the driver must exercise *both* patterns — a full session transition cycle (State) *and* a multi-note dispense plus a shortage rejection (Chain). One example, two proofs.

---

## Step 10 — Talk through the follow-ups

1. **Amount not makeable → reject the whole transaction.** Already handled: `canDispense` runs on a snapshot *before* any debit or `remove`. No partial dispense, so no cleanup. State this guarantee explicitly — it's follow-up 1, solved by design.

2. **Concurrent access to the shared cash store.** `Bank` and `CashInventory` are shared by reference, so two machines can race on the last ₹2000 note. Guard the **check-then-dispense** as one critical section: a `std::mutex` in `CashInventory` held across `canDispense` + the chain run (or compare-and-swap per denom). The State and Chain patterns are untouched — you're adding a lock around the mutation, not redesigning. The same applies to `debit` on a shared `Account`.

3. **Logging and rollback on a mid-way dispense failure.** With feasibility checked first, a *correct* run can't fail mid-chain — but hardware jams happen. Make the operation transactional: record the debit + intended notes to a journal *before* dispensing; if the chain throws partway, **rollback** = credit the account back and restore the note counts from the journal, then surface the error. Because both the balance debit and the note removals are reversible, rollback is a replay-in-reverse — the same reversibility payoff as undo via reversible counters.

> **Thinking habit:** end by showing each new requirement maps to an *existing seam* — a lock around the resource, a journal beside the mutation, a new handler in the chain — not a rewrite. That open/closed property is what the interviewer wants to hear.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — two named patterns ⇒ split along the seam (session control vs. cash mechanics).
2. **Nouns → two class families**: State (`ATMState` + concretes) and Chain (`CashDispenseHandler` + concretes), meeting only inside `ATM`.
3. **Interface first** — `ATM`'s public methods are *pure delegation*; the chain node knows only its successor.
4. **Leaves first** (`Card`, `Account`, `Bank`, `CashInventory`); model physical cash separately from logical balance.
5. **Chain of Responsibility** for dispensing — largest-denom-first *is* the greedy "fewest notes" rule.
6. **Feasibility on a snapshot, then mutate** — three guards (amount valid, balance, notes) before any money moves.
7. **Context owns data + driver-helpers**; states are **stateless** and own their **transitions**.
8. **Own both families once** (`unique_ptr`), pass non-owning pointers for "current state" and "chain head."
9. **Driver proves both patterns**; **follow-ups = lock / journal / new handler**, never a rewrite.

Follow that skeleton on any "machine with modes + a request pipeline" LLD (ATM, payment gateway, request middleware) and both patterns fall out almost mechanically.
