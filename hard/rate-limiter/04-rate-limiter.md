# Rate Limiter — LLD Problem Statement

**Difficulty:** Hard
**Language:** C++
**Pattern focus:** Strategy (algorithms) + thread safety + Singleton/Factory config

---

## Context
Design a reusable rate limiter that caps how many requests a client may make in a time window — embeddable in an API gateway or service middleware.

## Functional Requirements
- `allowRequest(clientId)` returns whether a request is permitted **right now**.
- Support **multiple algorithms**, swappable per configuration:
  - **Token Bucket** — tokens refill at a fixed rate; each request consumes one.
  - **Leaky Bucket** — requests queue and drain at a fixed rate.
  - **Fixed Window Counter** — N requests per fixed time window.
  - **Sliding Window Log / Counter** — smooths the fixed-window boundary burst.
- **Per-client** limits (each `clientId` has its own state).

## Non-Functional / Constraints
- **Thread-safe**: high-concurrency `allowRequest` calls on the same client must be correct.
- The algorithm is a **Strategy** so it swaps without changing call sites.
- Config (limits per client/tier) via a **Singleton** config or a **Factory** producing limiters.
- Efficient: O(1) per check; bounded memory per client.

## Expected Public Interface
```cpp
class RateLimitStrategy {              // Strategy
public:
    virtual bool allowRequest(const std::string& clientId) = 0;
    virtual ~RateLimitStrategy() = default;
};

// e.g. TokenBucketLimiter, FixedWindowLimiter, SlidingWindowLimiter

class RateLimiterFactory {
public:
    static std::unique_ptr<RateLimitStrategy> create(AlgoType type, const RateLimitConfig& cfg);
};

class RateLimiter {
public:
    explicit RateLimiter(std::unique_ptr<RateLimitStrategy> strategy);
    bool allowRequest(const std::string& clientId);   // thread-safe
};
```

## What the Interviewer Is Really Testing
- Correct, **clearly-explained algorithms** (token refill math; why sliding window beats fixed window at boundaries).
- The **Strategy** abstraction and a **Factory** that produces the right limiter.
- The **thread-safety** story per client (per-key lock / atomic token count; avoid one global lock).
- Bounded memory: how you evict stale client state.

## Follow-Up Questions to Expect
1. **Distributed** rate limiting across many service instances (centralized store / Redis-style counters) — discuss even if you implement in-memory.
2. **Tiered limits** (free vs premium clients) and dynamic config reload.
3. **Burst allowance** vs steady rate trade-offs.
4. Returning `Retry-After` / time-until-allowed rather than a bare boolean.

## Your Task
1. Assumptions + interface, then implement Token Bucket and Fixed Window behind the Strategy.
2. Make per-client state thread-safe with fine-grained locking.
3. Add a Factory + config; discuss the distributed follow-up.
