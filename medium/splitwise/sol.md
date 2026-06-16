# Splitwise (Expense Sharing) — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **Strategy pattern** and a clean **balance model** feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"Split logic must be a **Strategy** (`SplitStrategy`) so new split types drop in without touching the expense engine."* → This is **the** thing being tested. An `if (type == EQUAL) … else if (EXACT) …` block inside `addExpense` is a fail. EQUAL / EXACT / PERCENT each become a class.
2. *"Validate splits (exact sums to total; percentages sum to 100)."* → Validation is **per split type**, so it belongs *inside each strategy*, not in the manager.
3. *"Maintain a balance sheet: for each user, net amount owed to / owed by each other user."* → The core data structure is **pairwise net balances**, updated transactionally per expense. Getting this right is half the score.
4. *"`getBalances` reads cleanly from your data structure."* → Choose the balance representation so the query is a near-trivial read, not a recompute.
5. Follow-ups name the seams: **debt simplification** (min-cash-flow greedy), multi-currency, edit/delete with recompute.

> **Thinking habit:** when the prompt literally says "must be a Strategy," it's naming the pattern. Build it; the only real thinking left is the data structure behind it.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *user, group, expense, payer, participant, amount, split, split type, balance, settle-up.*

| Class | Owns | Why it exists |
|-------|------|---------------|
| `User` | id, name | identity of who pays / owes |
| `Group` | name, member ids | scopes "who owes whom" queries |
| `Split` | a user + their share of one expense | one participant's slice of an expense |
| `SplitStrategy` (interface) | how an amount becomes splits + its validation | one algorithm per split type |
| `EqualSplit` / `ExactSplit` / `PercentSplit` | the actual math + rule for that type | the concrete strategies |
| `Expense` | payer, amount, the resulting splits | an immutable record of one shared cost |
| `ExpenseManager` (the orchestrator) | users, groups, the **balance sheet** | applies expenses, settles, answers queries |

> **Thinking habit:** Strategy pattern = one **interface** + N **concrete algorithms** + one **client** (the manager) that holds a reference to the interface. Spot those three roles and the class list writes itself.

---

## Step 2 — Pin the public interface (the contract)

Given to us — lock it before internals:

```cpp
enum class SplitType { EQUAL, EXACT, PERCENT };

class SplitStrategy {                 // Strategy
public:
    virtual std::vector<Split> computeSplits(double amount,
                                             const std::vector<User>& participants,
                                             const std::vector<double>& values) = 0;
    virtual ~SplitStrategy() = default;
};

class ExpenseManager {
public:
    void addExpense(const std::string& payerId,
                    double amount,
                    const std::vector<std::string>& participantIds,
                    SplitType type,
                    const std::vector<double>& values);   // values used by EXACT/PERCENT
    void settleUp(const std::string& fromId, const std::string& toId, double amount);
    std::map<std::string,double> getBalances(const std::string& userId) const;  // +owed, -owes
};
```

Decisions baked in here:
- `computeSplits` is the **only** thing a strategy exposes. It takes the raw inputs and returns *who owes what slice*; validation throws from inside it.
- `values` is the escape hatch for the type-specific data: ignored by `EQUAL`, exact amounts for `EXACT`, percentages for `PERCENT`. One signature, three behaviours — that's the Strategy contract.
- `getBalances` returns a **map of other-user-id → signed amount**: positive means they owe *you*, negative means *you* owe them. The sign convention is the whole readability of the API.

> **Thinking habit:** fix the sign convention (`+` = owed to me, `−` = I owe) in the contract and write it in a comment. Every later bug is a sign bug; decide once.

---

## Step 3 — Model the leaves: `User`, `Group`, `Split`, `Expense`

Bottom-up: things with no dependencies first.

```cpp
struct User {
    std::string id;
    std::string name;
};

struct Group {
    std::string name;
    std::vector<std::string> memberIds;
};
```

A `Split` is the atom the strategy produces — *this user owes this much of this expense*:

```cpp
struct Split {
    std::string userId;
    double amount = 0.0;   // this participant's share (>= 0)
};
```

An `Expense` is just an immutable record: who paid, how much, and the resulting splits. It carries no logic — the math already happened in the strategy.

```cpp
struct Expense {
    std::string payerId;
    double amount = 0.0;
    std::vector<Split> splits;   // sum of split.amount == amount
};
```

> **Thinking habit:** make the record types dumb. `Expense` is *the output* of the algorithm, so it should hold results, not recompute them. Logic lives in the strategy; data lives in the leaf.

---

## Step 4 — The key insight #1: the Strategy shape (split types)

Three roles, wired together:

```
  ExpenseManager (client)
        │ picks a strategy by SplitType
        ▼
  SplitStrategy (interface)  ── computeSplits(amount, participants, values)
        ▲          ▲          ▲
        │          │          │
   EqualSplit  ExactSplit  PercentSplit
   (÷ evenly)  (use values, (use values as %,
                sum==total)  sum==100)
```

- The **interface** has exactly one method; each concrete class owns *both* the math *and* the validation rule for its type.
- The **client** (`ExpenseManager`) never branches on `SplitType` to do math — it maps the enum to a strategy object once, then calls `computeSplits`. Adding `SHARES` (weighted) later = one new subclass, zero edits to the manager. That's the open/closed payoff the problem demanded.

```cpp
class SplitStrategy {
public:
    virtual ~SplitStrategy() = default;
    virtual std::vector<Split> computeSplits(double amount,
                                             const std::vector<User>& participants,
                                             const std::vector<double>& values) = 0;
};
```

Now the three concrete strategies. Note how **validation is the first thing each does** — reject before producing any split.

```cpp
// ---------- EQUAL: ignore `values`, divide evenly ----------
class EqualSplit : public SplitStrategy {
public:
    std::vector<Split> computeSplits(double amount,
                                     const std::vector<User>& participants,
                                     const std::vector<double>&) override {
        if (participants.empty())
            throw std::invalid_argument("no participants");

        std::vector<Split> splits;
        double share = amount / participants.size();
        double assigned = 0.0;
        for (std::size_t i = 0; i < participants.size(); ++i) {
            // give the last participant the rounding remainder so the sum is exact
            double s = (i + 1 == participants.size()) ? (amount - assigned) : share;
            splits.push_back({participants[i].id, s});
            assigned += s;
        }
        return splits;
    }
};

// ---------- EXACT: `values[i]` is participant i's amount; must sum to total ----------
class ExactSplit : public SplitStrategy {
public:
    std::vector<Split> computeSplits(double amount,
                                     const std::vector<User>& participants,
                                     const std::vector<double>& values) override {
        if (values.size() != participants.size())
            throw std::invalid_argument("one exact value per participant required");

        double sum = 0.0;
        for (double v : values) sum += v;
        if (std::abs(sum - amount) > kEps)
            throw std::invalid_argument("exact splits must sum to the total");

        std::vector<Split> splits;
        for (std::size_t i = 0; i < participants.size(); ++i)
            splits.push_back({participants[i].id, values[i]});
        return splits;
    }
private:
    static constexpr double kEps = 1e-9;
};

// ---------- PERCENT: `values[i]` is participant i's %; must sum to 100 ----------
class PercentSplit : public SplitStrategy {
public:
    std::vector<Split> computeSplits(double amount,
                                     const std::vector<User>& participants,
                                     const std::vector<double>& values) override {
        if (values.size() != participants.size())
            throw std::invalid_argument("one percentage per participant required");

        double sum = 0.0;
        for (double v : values) sum += v;
        if (std::abs(sum - 100.0) > kEps)
            throw std::invalid_argument("percentages must sum to 100");

        std::vector<Split> splits;
        for (std::size_t i = 0; i < participants.size(); ++i)
            splits.push_back({participants[i].id, amount * values[i] / 100.0});
        return splits;
    }
private:
    static constexpr double kEps = 1e-9;
};
```

> **Thinking habit:** floating money never compares with `==`. Use an epsilon for the sum checks, and hand any rounding remainder to one participant so the splits sum *exactly* to the total. Say this out loud — it's the detail that separates a careful answer.

---

## Step 5 — The key insight #2: the balance model

This is the other half of the score, and it's where naive answers leak. Spend real thought here.

**What to store.** The clean representation is a **nested map of pairwise net balances**:

```cpp
// balance_[A][B] = how much A owes B (positive) or B owes A (negative).
std::unordered_map<std::string,
    std::unordered_map<std::string, double>> balance_;
```

We keep it **antisymmetric**: every update touches both directions so `balance_[A][B] == -balance_[B][A]` always holds. That invariant makes `getBalances` a one-line read and makes settle-up trivial.

**Applying one expense.** The payer fronted the whole `amount`; each participant owes their split *back to the payer*. So for every split (skipping the payer's own share — you don't owe yourself):

```cpp
balance_[participant][payer] += share;   // participant now owes payer more
balance_[payer][participant] -= share;   // mirror, keep antisymmetry
```

That's it — each expense is a **batch of pairwise increments**. No recompute, no scan of history. The balance sheet *is* the running state.

**Why nested-map and not a flat `pairId → amount`?** Because the headline query is *"all balances for user X"* — with the nested map that's just `balance_[X]`, already in the exact shape `getBalances` returns.

> **Thinking habit:** pick the data structure by the query you must answer fastest. "Balances for one user" → make that user the outer key. The structure should make the common read trivial.

---

## Step 6 — Orchestrate with `ExpenseManager`

The manager wires the pieces: it owns the strategies (one instance each — they're stateless), resolves ids to `User`s, runs the chosen strategy, then folds the resulting splits into the balance sheet.

```cpp
class ExpenseManager {
public:
    ExpenseManager() {
        strategies_[SplitType::EQUAL]   = std::make_unique<EqualSplit>();
        strategies_[SplitType::EXACT]   = std::make_unique<ExactSplit>();
        strategies_[SplitType::PERCENT] = std::make_unique<PercentSplit>();
    }

    void addUser(const std::string& id, const std::string& name) {
        users_[id] = {id, name};
    }

    void addExpense(const std::string& payerId,
                    double amount,
                    const std::vector<std::string>& participantIds,
                    SplitType type,
                    const std::vector<double>& values) {
        // 1. resolve participants
        std::vector<User> participants;
        for (const auto& pid : participantIds)
            participants.push_back(users_.at(pid));   // throws if unknown

        // 2. delegate the math to the strategy (validates internally)
        std::vector<Split> splits =
            strategies_.at(type)->computeSplits(amount, participants, values);

        // 3. fold splits into the pairwise balance sheet
        for (const auto& s : splits) {
            if (s.userId == payerId) continue;       // you don't owe yourself
            balance_[s.userId][payerId] += s.amount;
            balance_[payerId][s.userId] -= s.amount;
        }
    }

    void settleUp(const std::string& fromId, const std::string& toId, double amount) {
        // `from` pays `to`, reducing what `from` owes `to`.
        balance_[fromId][toId] -= amount;
        balance_[toId][fromId] += amount;
    }

    // +ve => the other user owes `userId`; -ve => `userId` owes the other user.
    std::map<std::string, double> getBalances(const std::string& userId) const {
        std::map<std::string, double> out;
        auto it = balance_.find(userId);
        if (it == balance_.end()) return out;
        for (const auto& [other, owed] : it->second)
            if (std::abs(owed) > 1e-9)               // hide settled (zero) pairs
                out[other] = -owed;   // stored "userId owes other"; flip to the API's sign
        return out;
    }

private:
    std::unordered_map<std::string, User> users_;
    std::unordered_map<SplitType, std::unique_ptr<SplitStrategy>> strategies_;
    std::unordered_map<std::string,
        std::unordered_map<std::string, double>> balance_;
};
```

Two things to call out in an interview:
- **`addExpense` contains no split math and no validation** — it resolves, delegates, folds. The branching on `SplitType` is a single map lookup, never an `if/else` chain.
- **`settleUp` is just a balance decrement** — the same antisymmetric update as an expense, which is why "a payment" and "an expense" share one mechanism.

> **Thinking habit:** the orchestrator should read like a sentence: *resolve → delegate → apply*. If business math creeps into it, push that math back down into a strategy or a leaf.

---

## Step 7 — Prove it with a tiny driver

Always show a `main` that exercises each split type, a settle-up, and a rejected (invalid) split. It doubles as your test.

```cpp
#include <iostream>

int main() {
    ExpenseManager mgr;
    mgr.addUser("alice", "Alice");
    mgr.addUser("bob",   "Bob");
    mgr.addUser("carol", "Carol");

    // Alice pays 90, split EQUALLY among all three -> bob & carol owe 30 each.
    mgr.addExpense("alice", 90.0, {"alice", "bob", "carol"},
                   SplitType::EQUAL, {});

    // Bob pays 100, EXACT: alice 60, bob 40 (sums to 100).
    mgr.addExpense("bob", 100.0, {"alice", "bob"},
                   SplitType::EXACT, {60.0, 40.0});

    for (const auto& [other, amt] : mgr.getBalances("alice"))
        std::cout << "alice vs " << other << ": " << amt << "\n";

    // Carol settles her 30 to Alice.
    mgr.settleUp("carol", "alice", 30.0);

    // Invalid PERCENT (sums to 90, not 100) is rejected, not silently applied.
    try {
        mgr.addExpense("alice", 50.0, {"alice", "bob"},
                       SplitType::PERCENT, {40.0, 50.0});
    } catch (const std::exception& e) {
        std::cout << "Rejected: " << e.what() << "\n";
    }
    return 0;
}
```

> **Thinking habit:** a driver that hits all three strategies *and* one rejected split proves both the polymorphism and the validation in one go.

---

## Step 8 — Talk through the follow-ups (debt simplification is the headline)

1. **Debt simplification (min-cash-flow).** Reduce the number of transactions needed to settle a group. The trick: collapse the pairwise graph into **one net figure per person** — sum every row of the balance sheet so each user is a single number (creditor `+`, debtor `−`; the total is always 0). Then greedily match the **biggest debtor to the biggest creditor**, transfer `min(|debt|, credit|)`, and repeat:

   ```cpp
   // net[u] = total this user is owed (+) or owes (-)
   std::vector<std::pair<std::string,double>> simplify(
           const std::unordered_map<std::string,double>& net) {
       std::vector<std::pair<std::string,double>> creditors, debtors, txns;
       for (auto& [u, v] : net) {
           if (v >  1e-9) creditors.push_back({u, v});
           if (v < -1e-9) debtors.push_back({u, -v});  // store as positive owed
       }
       // settle largest-against-largest until everyone is zero
       std::size_t i = 0, j = 0;
       while (i < debtors.size() && j < creditors.size()) {
           double pay = std::min(debtors[i].second, creditors[j].second);
           txns.push_back({debtors[i].first + "->" + creditors[j].first, pay});
           debtors[i].second   -= pay;
           creditors[j].second -= pay;
           if (debtors[i].second   < 1e-9) ++i;
           if (creditors[j].second < 1e-9) ++j;
       }
       return txns;
   }
   ```

   Be honest in the interview: this greedy gives a **good, not provably minimal** answer (minimizing transactions exactly is NP-hard — it's partition-flavoured). Naming that limit is what scores.

2. **Multi-currency.** Store each `Expense` in its native currency and a timestamp; convert to a group base currency at expense time via an injected `ExchangeRateProvider` (itself a Strategy). Balances stay in the base currency so the math above is untouched.

3. **Edit / delete an expense.** Keep the original `Expense`'s splits. To delete, **subtract** the same pairwise increments you added (the balance updates are reversible — a payoff of storing increments rather than recomputing). To edit, delete-then-re-add. This is why `addExpense`'s fold is symmetric.

> **Thinking habit:** good LLD answers end by pointing at the extension points and naming the pattern (Strategy for rates) *and* the limit (greedy ≠ optimal). It proves your abstractions weren't accidental.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — "must be a Strategy" names the pattern; "net owed per user" names the data structure.
2. **Three Strategy roles**: interface (`SplitStrategy`), concretes (`Equal/Exact/Percent`), client (`ExpenseManager`).
3. **Interface first** — one `computeSplits` method; validation lives *inside* each strategy.
4. **Leaves first** (`User`, `Group`, `Split`, `Expense`) — records are dumb; logic stays in the strategy.
5. **Balance model is the other half** — antisymmetric nested map, shaped so `getBalances` is a trivial read.
6. **Orchestrator reads as resolve → delegate → apply**; expense and settle-up share one increment.
7. **Driver as proof** (all three splits + a rejection), then **name the seams** (greedy simplification, currency Strategy, reversible edits).

Follow that skeleton on any "track relationships / apply transactions / answer balance queries" LLD (ledgers, inventory transfers, points systems) and the Strategy-plus-balance-sheet design falls out almost mechanically.
