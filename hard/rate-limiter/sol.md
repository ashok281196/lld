# Rate Limiter — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **Strategy pattern + per-client locking** feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"Support **multiple algorithms**, swappable per configuration… the algorithm is a **Strategy** so it swaps without changing call sites."* → This is **the** thing being tested. A single hardcoded algorithm with a `switch (algoType)` inside `allowRequest` is a fail. The whole solution is a textbook **Strategy pattern**.
2. *"**Thread-safe**: high-concurrency `allowRequest` calls on the same client must be correct."* → Concurrency is a first-class requirement. And the hint is explicit: *"avoid one global lock."* You must reason about **per-client** locking, not a single mutex.
3. *"**Per-client** limits (each `clientId` has its own state)."* → State is a `clientId → bucket` map. The map itself is shared; each entry is independently lockable.
4. *"Efficient: O(1) per check; bounded memory per client."* → Token-bucket math must be lazy (compute refill on read, no background timer). And you must have an eviction story for stale clients.
5. Follow-ups name the seams: distributed counters (Redis), tiered config + dynamic reload, burst vs steady, returning `Retry-After`.

> **Thinking habit:** when the prompt says "multiple algorithms, swappable… Strategy," it is literally naming the pattern. Build the pattern, don't fight it — then read the *non-functional* lines (thread-safe, O(1)) because those pick your data structures.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *request, client, limiter, strategy, algorithm, token, bucket, window, config, tier, factory.*

| Class | Owns | Why it exists |
|-------|------|---------------|
| `RateLimitConfig` | capacity, refill rate, window size | the knobs each algorithm reads |
| `RateLimitStrategy` (interface) | the single `allowRequest(clientId)` op | one reaction per algorithm — the swappable part |
| `TokenBucketLimiter` / `FixedWindowLimiter` | per-client buckets + refill/reset math | the concrete algorithms |
| `RateLimiterFactory` | `AlgoType + config → strategy` | builds the right strategy; call sites stay algorithm-blind |
| `RateLimiter` | a strategy + the thread-safety wrapper | the public façade clients actually call |

> **Thinking habit:** Strategy pattern = one **Context/façade** (`RateLimiter`) + one **Strategy interface** + N **concrete strategies**, plus a **Factory** to hide construction. Spot those roles and the class list writes itself.

---

## Step 2 — Pin the public interface (the contract)

Given to us — lock it before internals:

```cpp
enum class AlgoType { TOKEN_BUCKET, FIXED_WINDOW, SLIDING_WINDOW };

struct RateLimitConfig {
    int    capacity   = 10;   // max tokens / max requests per window
    double refillRate = 5.0;  // tokens per second (token bucket)
    int    windowMs   = 1000; // window length in ms (window algorithms)
};

class RateLimitStrategy {                          // Strategy
public:
    virtual bool allowRequest(const std::string& clientId) = 0;
    virtual ~RateLimitStrategy() = default;
};

class RateLimiterFactory {
public:
    static std::unique_ptr<RateLimitStrategy> create(AlgoType type, const RateLimitConfig& cfg);
};

class RateLimiter {                                // façade
public:
    explicit RateLimiter(std::unique_ptr<RateLimitStrategy> strategy);
    bool allowRequest(const std::string& clientId);   // thread-safe
};
```

Decisions baked in here:
- **`allowRequest` returns `bool`** — permitted *right now* or not. (The `Retry-After` follow-up extends this to a richer result; see Step 8.)
- **The strategy is injected**, not chosen inside `RateLimiter`. The façade never names an algorithm. That single idea is the whole pattern.
- **Construction goes through a Factory**, so adding an algorithm touches one `switch` in one place, not every call site.

> **Thinking habit:** the interface is a promise. Lock the promise (a `bool` from one method), then you're free to swap token-bucket for sliding-window behind it with zero blast radius.

---

## Step 3 — Model the leaves: `RateLimitConfig` and the per-client `Bucket`

Bottom-up: things with no dependencies first. `RateLimitConfig` (above) is a plain struct of knobs.

The other leaf is the **per-client state** each algorithm keeps. For token bucket it's a token count + a last-refill timestamp; for fixed window it's a counter + a window-start timestamp. Crucially, **each entry carries its own mutex** so two clients never block each other.

```cpp
#include <chrono>
#include <mutex>

using Clock     = std::chrono::steady_clock;   // monotonic — never jumps backward
using TimePoint = Clock::time_point;

struct TokenBucketState {
    double    tokens;        // fractional tokens allowed (refill is rate * elapsed)
    TimePoint lastRefill;
    std::mutex mtx;          // per-client lock — NOT one global lock
};

struct FixedWindowState {
    int       count;         // requests seen in the current window
    TimePoint windowStart;
    std::mutex mtx;
};
```

> **Thinking habit:** use `steady_clock`, never `system_clock`, for elapsed-time math — a wall-clock NTP adjustment must not retroactively grant or revoke tokens. Put the mutex *inside* the per-client state; that is what makes "fine-grained locking" fall out for free.

---

## Step 4 — The key insight: lazy token-bucket math (and why sliding beats fixed)

This is the heart of the problem. Spend real thought here.

**Token Bucket — the lazy-refill trick.** A naive answer runs a background thread that adds tokens every tick. Don't. Instead compute refill **on read**:

- Each client has `tokens` and `lastRefill`.
- On `allowRequest`: let `elapsed = now - lastRefill`. Add `elapsed * refillRate` tokens, **cap at `capacity`**, set `lastRefill = now`.
- If `tokens >= 1`, decrement by 1 and **allow**; else **deny**.

Why it works: tokens accrue continuously at `refillRate/sec`; we just materialize that accrual the moment someone asks. No timer, O(1) per check, and the `capacity` cap is exactly the **burst allowance** (a client who's been quiet can spend a full bucket at once).

```cpp
//                 capacity = 10, refillRate = 5/s
//  t=0   bucket full (10) ──┐
//  spend 10 instantly (burst)│  then ~5/s steady-state thereafter
//                            ▼
//  tokens = min(capacity, tokens + elapsedSeconds * refillRate)
```

**Fixed Window Counter.** Keep `count` and `windowStart`. If `now - windowStart >= window`, reset `count = 0`, `windowStart = now`. Allow while `count < capacity`, incrementing on allow.

**Why sliding window beats fixed — the boundary burst.** Fixed window lets a client send `capacity` at the *end* of window A and another `capacity` at the *start* of window B — `2 * capacity` in a hair over zero seconds across the boundary. Sliding window fixes this by weighting the previous window's count by the fraction still in view:

```
estimated = prevCount * (overlapFraction) + currentCount
allow if estimated < capacity
```

That smooths the seam at the cost of remembering one extra counter.

> **Thinking habit:** when a constraint says "O(1), no background work," reach for **lazy/on-read computation** — store a timestamp and reconstruct state from elapsed time instead of mutating it on a timer. And always be able to say *out loud* why one algorithm is chosen over another (boundary burst) — that's the "clearly-explained algorithms" the interviewer wants.

---

## Step 5 — Implement the strategies (each owns its map + per-client locks)

Each concrete strategy owns a `clientId → state` map. Two locks are at play: a short-held lock to **find/create** the entry, then the entry's **own** lock for the read-modify-write. Holding only the per-client lock during the math is what lets thousands of distinct clients proceed in parallel.

```cpp
#include <unordered_map>
#include <memory>

class TokenBucketLimiter : public RateLimitStrategy {
public:
    explicit TokenBucketLimiter(const RateLimitConfig& cfg) : cfg_(cfg) {}

    bool allowRequest(const std::string& clientId) override {
        TokenBucketState& st = stateFor(clientId);     // map lock held briefly inside
        std::lock_guard<std::mutex> lock(st.mtx);       // per-CLIENT lock only

        TimePoint now = Clock::now();
        double elapsedSec =
            std::chrono::duration<double>(now - st.lastRefill).count();

        st.tokens     = std::min((double)cfg_.capacity,
                                 st.tokens + elapsedSec * cfg_.refillRate);
        st.lastRefill = now;

        if (st.tokens >= 1.0) { st.tokens -= 1.0; return true; }  // spend a token
        return false;                                             // throttled
    }

private:
    // Find-or-create the client's bucket. The MAP mutex protects only the
    // lookup/insert, not the per-client math — that's the fine-grained part.
    TokenBucketState& stateFor(const std::string& clientId) {
        std::lock_guard<std::mutex> mapLock(mapMtx_);
        auto it = states_.find(clientId);
        if (it == states_.end()) {
            auto st = std::make_unique<TokenBucketState>();
            st->tokens     = cfg_.capacity;     // new clients start with a full bucket
            st->lastRefill = Clock::now();
            it = states_.emplace(clientId, std::move(st)).first;
        }
        return *it->second;
    }

    RateLimitConfig cfg_;
    std::mutex      mapMtx_;
    // unique_ptr so the stored object (and its mutex) never moves when the map rehashes.
    std::unordered_map<std::string, std::unique_ptr<TokenBucketState>> states_;
};
```

> ⚠️ **Why `unique_ptr<State>` and not `State` by value?** A `std::mutex` is neither movable nor copyable, and an `unordered_map` may rehash. Storing the state behind a pointer keeps the address (and the live mutex) **stable** across inserts. Returning a `State&` to a heap object is then safe to lock outside the map lock.

`FixedWindowLimiter` is the same shape — only the per-client math changes:

```cpp
class FixedWindowLimiter : public RateLimitStrategy {
public:
    explicit FixedWindowLimiter(const RateLimitConfig& cfg) : cfg_(cfg) {}

    bool allowRequest(const std::string& clientId) override {
        FixedWindowState& st = stateFor(clientId);
        std::lock_guard<std::mutex> lock(st.mtx);

        TimePoint now = Clock::now();
        auto window = std::chrono::milliseconds(cfg_.windowMs);
        if (now - st.windowStart >= window) {     // window expired -> reset
            st.count       = 0;
            st.windowStart = now;
        }
        if (st.count < cfg_.capacity) { ++st.count; return true; }
        return false;
    }

private:
    FixedWindowState& stateFor(const std::string& clientId) {
        std::lock_guard<std::mutex> mapLock(mapMtx_);
        auto it = states_.find(clientId);
        if (it == states_.end()) {
            auto st = std::make_unique<FixedWindowState>();
            st->count = 0; st->windowStart = Clock::now();
            it = states_.emplace(clientId, std::move(st)).first;
        }
        return *it->second;
    }

    RateLimitConfig cfg_;
    std::mutex      mapMtx_;
    std::unordered_map<std::string, std::unique_ptr<FixedWindowState>> states_;
};
```

> **Thinking habit:** two-level locking — a brief lock to *locate* shared structure, then the *element's own* lock to *mutate* it. Never hold the coarse lock during the per-client work, or you've rebuilt the "one global lock" the problem told you to avoid.

---

## Step 6 — The Factory and the façade

The **Factory** is the one place that knows the algorithm enum. Adding `SLIDING_WINDOW` later is a single new `case` — every call site stays untouched (open/closed).

```cpp
std::unique_ptr<RateLimitStrategy>
RateLimiterFactory::create(AlgoType type, const RateLimitConfig& cfg) {
    switch (type) {
        case AlgoType::TOKEN_BUCKET:  return std::make_unique<TokenBucketLimiter>(cfg);
        case AlgoType::FIXED_WINDOW:  return std::make_unique<FixedWindowLimiter>(cfg);
        case AlgoType::SLIDING_WINDOW:/* return std::make_unique<SlidingWindowLimiter>(cfg); */
        default: throw std::invalid_argument("unknown algorithm");
    }
}
```

The **façade** owns the chosen strategy and exposes the clean public call. Because each strategy already locks per client, the façade adds *no* lock of its own — it would only reintroduce a global bottleneck.

```cpp
class RateLimiter {
public:
    explicit RateLimiter(std::unique_ptr<RateLimitStrategy> strategy)
        : strategy_(std::move(strategy)) {}

    // Thread-safe because the STRATEGY locks per client. No global lock here on purpose.
    bool allowRequest(const std::string& clientId) {
        return strategy_->allowRequest(clientId);
    }

private:
    std::unique_ptr<RateLimitStrategy> strategy_;
};
```

For the *config* role, a **Singleton** holds per-tier limits and hands the right `RateLimitConfig` to the factory:

```cpp
class ConfigRegistry {                 // Singleton
public:
    static ConfigRegistry& instance() { static ConfigRegistry r; return r; }
    RateLimitConfig configFor(const std::string& tier) {
        std::lock_guard<std::mutex> lock(mtx_);
        return tiers_.count(tier) ? tiers_[tier] : tiers_["default"];
    }
    void set(const std::string& tier, const RateLimitConfig& c) {
        std::lock_guard<std::mutex> lock(mtx_);
        tiers_[tier] = c;               // dynamic reload = just overwrite under the lock
    }
private:
    ConfigRegistry() { tiers_["default"] = {}; }
    std::mutex mtx_;
    std::unordered_map<std::string, RateLimitConfig> tiers_;
};
```

> **Thinking habit:** keep the locking responsibility at exactly **one** layer. The strategy locks per client; the façade stays lock-free. Two layers of locking is a deadlock waiting to happen and a throughput killer.

---

## Step 7 — Prove it with a tiny driver

Always show a `main` that exercises a happy path, a throttle, and a refill recovery. It doubles as your test.

```cpp
#include <iostream>
#include <thread>

int main() {
    RateLimitConfig cfg;
    cfg.capacity = 3; cfg.refillRate = 2.0;   // burst 3, then ~2/sec

    RateLimiter limiter(
        RateLimiterFactory::create(AlgoType::TOKEN_BUCKET, cfg));

    // Burst: first 3 allowed, 4th denied (bucket drained).
    for (int i = 0; i < 4; ++i)
        std::cout << "req " << i << ": "
                  << (limiter.allowRequest("alice") ? "OK" : "THROTTLED") << "\n";
    // OK, OK, OK, THROTTLED

    // Wait for refill: ~0.6s -> ~1.2 tokens -> one request recovers.
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    std::cout << "after wait: "
              << (limiter.allowRequest("alice") ? "OK" : "THROTTLED") << "\n";  // OK

    // A different client is independent (its own bucket, its own lock).
    std::cout << "bob: "
              << (limiter.allowRequest("bob") ? "OK" : "THROTTLED") << "\n";    // OK
    return 0;
}
```

> **Thinking habit:** a driver that hits the burst limit, the *recovery after refill*, and a second independent client proves the three things that matter — the math, the time-based refill, and per-client isolation — in 15 lines.

---

## Step 8 — Talk through the follow-ups (don't necessarily code them all)

Show the seams are already there:

1. **Distributed rate limiting.** In-memory state can't be shared across N service instances. Move the per-client state to a **centralized store** (Redis): token bucket becomes an atomic Lua script (`GET tokens`, refill by elapsed, `DECR`, `SET` with TTL) so the read-modify-write stays atomic across instances. The `RateLimitStrategy` interface is **unchanged** — `RedisTokenBucketLimiter` is just another strategy. That's the payoff of the abstraction.

2. **Tiered limits + dynamic reload.** Already seeded: `ConfigRegistry` maps tier → config, and `set()` overwrites under a lock. A free client gets `capacity=10`, premium `capacity=1000`; resolve the tier per request and the factory builds accordingly. Reload = call `set()` at runtime; existing buckets pick up new limits on their next refill.

3. **Burst vs steady-rate trade-off.** This is literally token bucket's two knobs: `capacity` is the burst you tolerate, `refillRate` is the sustained rate. Want strict steady rate, no bursts? Set `capacity = 1`, or switch to **leaky bucket** (drains at a fixed rate regardless of arrival pattern). Name the trade-off out loud.

4. **Return `Retry-After` instead of a bare bool.** Widen the result type:

   ```cpp
   struct Decision {
       bool allowed;
       std::chrono::milliseconds retryAfter;   // 0 if allowed
   };
   ```

   For token bucket, when denied, `retryAfter = ceil((1 - tokens) / refillRate)` — the time until the next whole token. The interface widens from `bool` to `Decision`; the algorithms already hold every value needed to compute it.

> **Thinking habit:** good LLD answers end by pointing at the extension points and naming the pattern that fits — a new requirement (Redis, tiers) becomes a *new strategy* or a *config tweak*, not a rewrite. That proves your abstractions weren't accidental.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — "multiple swappable algorithms" names Strategy; "thread-safe, avoid one global lock" picks per-client mutexes; "O(1), no timer" forces lazy refill.
2. **Roles**: façade (`RateLimiter`), Strategy interface, N concrete algorithms, Factory to build them, Singleton config.
3. **Interface first** — a `bool` from one method; inject the strategy, never name an algorithm in the façade.
4. **Leaves first** (`RateLimitConfig`, per-client `State` with its own mutex), use `steady_clock`.
5. **Lazy on-read math** beats background timers — store a timestamp, reconstruct from elapsed time.
6. **Two-level locking** — brief map lock to find/create, per-client lock for the math; store states behind `unique_ptr` so mutex addresses stay stable.
7. **Factory isolates the `switch`**; the façade adds no lock.
8. **Driver as proof** (burst → throttle → recovery → second client), then **name the seams** (Redis strategy, tiered config, `Retry-After`).

Follow that skeleton on any "pluggable algorithm under concurrency" LLD (cache eviction policies, load balancers, schedulers) and the Strategy-plus-fine-grained-locking design falls out almost mechanically.
