# Splitwise (Expense Sharing) — LLD Problem Statement

**Difficulty:** Medium
**Language:** C++
**Pattern focus:** Strategy (split types) + balance/graph modeling

---

## Context
Design an expense-sharing app: users record shared expenses, the system tracks who owes whom, and it can simplify debts.

## Functional Requirements
- Users belong to **groups** (and can have non-group friendships).
- Record an **expense**: a payer, an amount, and the set of participants with a **split type**:
  - **EQUAL** — divided evenly,
  - **EXACT** — explicit amount per participant (must sum to total),
  - **PERCENT** — percentage per participant (must sum to 100).
- Maintain a **balance sheet**: for each user, net amount owed to / owed by each other user.
- Show **"who owes whom"** for a user or a group.
- **Settle up**: record a payment that reduces a balance.

## Non-Functional / Constraints
- **Split logic must be a Strategy** (`SplitStrategy`) so new split types drop in without touching the expense engine.
- Validate splits (exact sums to total; percentages sum to 100).

## Expected Public Interface
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

## What the Interviewer Is Really Testing
- The **Strategy** for split types and the validation per type.
- A correct **balance model** (pairwise net balances, updated transactionally per expense).
- Whether `getBalances` reads cleanly from your data structure.

## Follow-Up Questions to Expect
1. **Debt simplification**: minimize the number of transactions to settle a group (this is the classic greedy / min-cash-flow algorithm — interviewers love this here).
2. **Multi-currency** expenses with conversion.
3. Expense **comments, attachments, edit/delete** with balance recomputation.

## Your Task
1. Assumptions + interface, then `User`, `Group`, `Expense`, `Split`, `ExpenseManager`.
2. Implement the three split strategies with validation.
3. Attempt debt simplification as the headline follow-up.
