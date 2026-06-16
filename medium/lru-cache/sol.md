# LRU Cache — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **hashmap + doubly-linked-list** trick feel inevitable — and the **Strategy pattern** for eviction feel like the natural seam, not an afterthought.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"Both `get` and `put` must be **O(1)** average time."* → This is **the** thing being tested. Any answer that scans the cache to find the least-recently-used entry is `O(n)` and fails. The whole solution exists to make "find + move + evict" all constant time.
2. *"The eviction policy must be **pluggable** (LRU base; LFU / FIFO as drop-ins via an `EvictionPolicy` interface)."* → The recency bookkeeping must live **behind an interface**, separate from the key→value storage. That's a **Strategy pattern**. The statement says this *is the point at SDE-3*.
3. *"**Thread-safe** for concurrent `get`/`put`."* → A mutex is a first-class requirement, not a footnote. `get` mutates recency state, so it is **not** a read-only operation — it needs the write lock too.
4. *"Generic over key and value types."* → Templates. Everything is `template <typename K, typename V>`.
5. Follow-ups name the seams: LFU swap, TTL, sharded locks, write-through. We leave room for them.

> **Thinking habit:** the non-functional constraints (O(1), pluggable, thread-safe) decide your data structures and your seams. Mine them before writing a line.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *cache, key, value, entry, capacity, policy, eviction, recency, lock.*

| Class | Owns | Why it exists |
|-------|------|---------------|
| `EvictionPolicy<K,V>` (interface) | the recency/frequency bookkeeping | decides *which key* to evict — the pluggable strategy |
| `LRUEvictionPolicy<K,V>` | a doubly-linked list of keys + a map into it | concrete LRU: O(1) "touch" and "find victim" |
| `Cache<K,V>` (the orchestrator) | key→value map, capacity, the policy, the mutex | the public API; delegates *which to evict* to the policy |

> **Thinking habit:** Strategy pattern = one **interface** (`EvictionPolicy`) + N **concrete strategies** (LRU/LFU/FIFO) + one **context** (`Cache`) that holds a strategy pointer. Spot those three roles and the class list writes itself.

---

## Step 2 — Pin the public interface (the contract)

Given to us — lock it before internals:

```cpp
template <typename K, typename V>
class EvictionPolicy {                 // Strategy
public:
    virtual void keyAccessed(const K& key) = 0;  // get or update touched a key
    virtual void keyAdded(const K& key)    = 0;   // a brand-new key was inserted
    virtual K    evictKey()                = 0;    // returns the key to remove
    virtual ~EvictionPolicy() = default;
};

template <typename K, typename V>
class Cache {
public:
    Cache(size_t capacity, std::unique_ptr<EvictionPolicy<K,V>> policy);
    std::optional<V> get(const K& key);            // std::nullopt on miss
    void             put(const K& key, const V& value);
};
```

Decisions baked in here:
- **A miss is `std::optional<V>` returning `std::nullopt`** — no sentinel values, no exceptions for the common case.
- The policy interface speaks **only in keys**, never values. The policy never stores `V`; it owns *ordering*, not data. That keeps storage and policy cleanly decoupled.
- The `Cache` **owns** the policy via `unique_ptr` — injected at construction (dependency injection). Swap LRU for LFU by passing a different object; `Cache`'s code never changes.

> **Thinking habit:** the interface is a promise. A policy that traffics only in keys is what lets storage and eviction evolve independently. Lock that boundary first.

---

## Step 3 — Model the leaves: the stored entry

Bottom-up: the thing with no dependencies first. `Cache` needs a key→value store. Reaching straight for `std::unordered_map<K, V>` works for *storage*, but think one move ahead: the policy will hand us a **key to evict**, and we'll need to delete it from the map — that's already O(1) on an `unordered_map`. Good.

So the leaf here is barely a type at all — it's the choice of container:

```cpp
std::unordered_map<K, V> store_;   // key -> value, O(1) average lookup
```

The interesting state lives **inside the policy**, not the cache. The cache holds *what* the values are; the policy holds *the order they should leave in*. Keep that split sharp.

> **Thinking habit:** don't over-model. The "leaf" can be a well-chosen standard container. Spend your modeling budget where the algorithm actually lives — here, inside the policy.

---

## Step 4 — The key insight: O(1) LRU via hashmap + doubly-linked-list

This is the heart of the problem. Spend real thought here.

**Naive idea:** stamp each entry with a "last used" timestamp; on eviction scan all entries for the smallest. Eviction is `O(n)` → fails the O(1) requirement.

**The trick — two structures cooperating:**

```
       MRU (front)                          LRU (back / evict here)
        ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐
list:   │  D  │ ⇄ │  A  │ ⇄ │  C  │ ⇄ │  B  │
        └─────┘   └─────┘   └─────┘   └─────┘
           ▲         ▲         ▲         ▲
map:   key → iterator (node address) for each key, O(1) lookup
```

- A **doubly-linked list of keys** orders them by recency: **front = most-recently-used**, **back = least-recently-used**.
- A **hashmap `key → list iterator`** gives O(1) access to *any* node so we can splice it out without walking the list.

Why **both** are mandatory (be ready to justify):
- The list gives **O(1) reordering** (move a touched key to the front) and **O(1) "who's the victim"** (it's always the back). But finding a node *by key* in a bare list is O(n).
- The map fixes exactly that: O(1) **find the node**. But a bare map has no notion of order.
- Together: find-by-key in O(1) (map) → unlink + push-front in O(1) (list, via `splice`). Eviction is O(1) (pop the back, erase from the map).

`std::list::splice` is the magic move — it relinks nodes **without** invalidating iterators, so the map's stored iterators stay valid after a reorder.

```cpp
#include <list>
#include <unordered_map>

template <typename K, typename V>
class LRUEvictionPolicy : public EvictionPolicy<K, V> {
public:
    // Key was read or updated -> it's now the most-recently-used.
    void keyAccessed(const K& key) override {
        auto it = pos_.find(key);
        if (it == pos_.end()) return;            // not tracked yet; keyAdded handles new keys
        // Move its node to the front in O(1); splice keeps `it->second` valid.
        order_.splice(order_.begin(), order_, it->second);
    }

    // Brand-new key -> insert at the front (most-recently-used).
    void keyAdded(const K& key) override {
        order_.push_front(key);
        pos_[key] = order_.begin();
    }

    // The victim is the back of the list (least-recently-used). Remove + return it.
    K evictKey() override {
        K victim = order_.back();
        order_.pop_back();
        pos_.erase(victim);
        return victim;
    }

private:
    std::list<K> order_;                              // front = MRU, back = LRU
    std::unordered_map<K, typename std::list<K>::iterator> pos_;  // key -> its node
};
```

Notice the policy holds **no values** — only keys and their order. That data-free-of-`V` discipline is what makes it a drop-in for LFU/FIFO later.

> **Thinking habit:** when "find it" and "reorder it" and "pick the extreme" all must be O(1), pair a **hashmap (random access)** with a **linked structure (ordering)**. The map locates; the list orders. This combo recurs across DS-design problems.

---

## Step 5 — Orchestrate with `Cache`: store, delegate, evict

`Cache` is the context. It holds the values and the mutex; it delegates *which key leaves* to the policy. Two operations:

- **`get`** — miss → `nullopt`. Hit → tell the policy this key was touched, return the value. (Note: `get` *mutates* recency, so it is a writer.)
- **`put`** — update existing → overwrite value + `keyAccessed`. New key → if at capacity, ask the policy for a victim and erase it from the store first, then insert + `keyAdded`.

Validate/branch in a fixed order so state never drifts.

```cpp
#include <mutex>
#include <optional>

template <typename K, typename V>
class Cache {
public:
    Cache(size_t capacity, std::unique_ptr<EvictionPolicy<K, V>> policy)
        : capacity_(capacity), policy_(std::move(policy)) {}

    std::optional<V> get(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = store_.find(key);
        if (it == store_.end()) return std::nullopt;   // miss
        policy_->keyAccessed(key);                      // touch: now MRU
        return it->second;
    }

    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = store_.find(key);
        if (it != store_.end()) {        // update existing key
            it->second = value;
            policy_->keyAccessed(key);
            return;
        }

        if (store_.size() >= capacity_) {     // at capacity -> evict first
            K victim = policy_->evictKey();
            store_.erase(victim);
        }

        store_[key] = value;             // insert new
        policy_->keyAdded(key);
    }

private:
    size_t capacity_;
    std::unordered_map<K, V> store_;                       // key -> value
    std::unique_ptr<EvictionPolicy<K, V>> policy_;         // the Strategy
    std::mutex mutex_;                                     // guards both above
};
```

Two design wins to call out in an interview:
- **`Cache` never decides recency.** It only asks "who's the victim?" The policy owns that. Swapping policies cannot break the cache.
- **Evict *before* insert.** Erasing the victim from `store_` keeps size invariant and avoids a transient over-capacity state.

> **Thinking habit:** the context coordinates; the strategy decides. Keep the "what" (storage) in the context and the "which/when" (policy) behind the interface, and either side can change alone.

---

## Step 6 — The thread-safety answer: one mutex, then talk contention

The simplest correct answer is **one `std::mutex` guarding every public method** — exactly what Step 5 does with `lock_guard`. Say this clearly:

- **`get` is a writer, not a reader.** It calls `keyAccessed`, which reorders the list. So you *cannot* use a shared/read lock for `get` — it would corrupt the list under concurrency. One plain mutex is correct.
- `lock_guard` is **RAII**: the lock releases when the function returns, even on an exception path. No manual unlock to forget.

Then volunteer the next step (the interviewer is waiting for it):

- **Contention** is the problem — one global lock serializes the whole cache. Fix with **sharding**: partition keys into `N` independent sub-caches by `hash(key) % N`, each with its own mutex + policy. Threads hitting different shards never block each other. (This is the Step-9 follow-up.)

> **Thinking habit:** state the *correct* simple answer first (one mutex), then name *why it doesn't scale* (contention) and the *standard fix* (sharding). Don't jump to the clever answer before the safe one.

---

## Step 7 — Prove it with a tiny driver

Always show a `main` that exercises a hit, a miss, and an eviction. It doubles as your test.

```cpp
#include <iostream>

int main() {
    auto policy = std::make_unique<LRUEvictionPolicy<int, std::string>>();
    Cache<int, std::string> cache(2, std::move(policy));   // capacity 2

    cache.put(1, "one");
    cache.put(2, "two");

    cache.get(1);              // touch 1 -> order: [1 (MRU), 2 (LRU)]
    cache.put(3, "three");     // at capacity -> evict LRU (key 2), insert 3

    if (!cache.get(2))         // 2 was evicted -> miss
        std::cout << "2 evicted (miss)\n";

    if (auto v = cache.get(1)) // 1 survived -> hit
        std::cout << "1 = " << *v << "\n";   // one

    if (auto v = cache.get(3)) // 3 present -> hit
        std::cout << "3 = " << *v << "\n";   // three
    return 0;
}
```

> **Thinking habit:** a driver that shows a survivor, an evicted victim, and a recency *touch* changing who gets evicted proves the LRU logic far better than any prose.

---

## Step 8 — Follow-up: swap to LFU to prove the abstraction holds

The real test of Step 0's "pluggable" claim: can we drop in **LFU** (evict the *least-frequently-used*) without touching `Cache`?

LFU keeps a **frequency count per key** and, among equal frequencies, breaks ties by recency. The O(1) structure is **frequency buckets**: a map `freq → (ordered list of keys at that frequency)`, plus `key → (freq, node)` lookups, plus a running `minFreq`.

```cpp
template <typename K, typename V>
class LFUEvictionPolicy : public EvictionPolicy<K, V> {
public:
    void keyAdded(const K& key) override {
        freqOf_[key] = 1;
        buckets_[1].push_front(key);
        pos_[key] = buckets_[1].begin();
        minFreq_  = 1;                       // a new key is always freq 1
    }

    void keyAccessed(const K& key) override {
        int f = freqOf_[key];
        buckets_[f].erase(pos_[key]);        // pull out of old bucket
        if (buckets_[f].empty() && f == minFreq_) ++minFreq_;
        int nf = f + 1;
        freqOf_[key] = nf;
        buckets_[nf].push_front(key);        // promote to next bucket
        pos_[key] = buckets_[nf].begin();
    }

    K evictKey() override {
        auto& lst = buckets_[minFreq_];      // lowest freq; back = least-recent there
        K victim = lst.back();
        lst.pop_back();
        freqOf_.erase(victim);
        pos_.erase(victim);
        return victim;
    }

private:
    int minFreq_ = 0;
    std::unordered_map<K, int> freqOf_;                  // key -> frequency
    std::unordered_map<int, std::list<K>> buckets_;      // frequency -> keys at it
    std::unordered_map<K, typename std::list<K>::iterator> pos_;
};
```

The payoff: **`Cache` is unchanged.** Same three calls (`keyAdded`, `keyAccessed`, `evictKey`), same `unique_ptr` injection — only the constructor argument differs:

```cpp
Cache<int, std::string> cache(2, std::make_unique<LFUEvictionPolicy<int, std::string>>());
```

FIFO is even simpler: `keyAccessed` is a no-op (order never changes on access), and `evictKey` pops the oldest. One more subclass, zero cache edits.

> **Thinking habit:** the proof that a Strategy is real is swapping one concrete strategy for another and the context not noticing. If `Cache` needed an edit to support LFU, the abstraction leaked.

---

## Step 9 — Talk through the remaining follow-ups

Show the seams are already there:

1. **TTL / expiry per entry.** Store an expiry timestamp alongside each value (`struct Entry { V value; time_point expiry; }`). On `get`, if `now > expiry`, treat as a miss and remove it (also tell the policy via an `evict`/`keyRemoved` hook). Capacity eviction and time eviction coexist: TTL is *lazy* on access (or a background sweeper); LRU/LFU handles the size bound.
2. **Reduce lock contention — sharded cache.** Wrap `N` independent `Cache` instances; route each key by `hash(key) % N` to its shard. Each shard has its own mutex + policy, so independent keys never contend. Capacity is split per shard (`total / N`). The single-cache code is reused verbatim — sharding is composition on top, not a rewrite.
3. **Write-through / write-back to a backing store.** Inject a `BackingStore` interface. *Write-through:* `put` writes the store synchronously before returning (consistent, slower). *Write-back:* mark the entry dirty, flush on eviction or on a timer (faster, risk of loss on crash). Another injected strategy beside the eviction policy — orthogonal concerns, separate interfaces.

> **Thinking habit:** good LLD answers end by mapping each follow-up to *a new injected interface or a composition*, never to an edit of the core class. That's the open/closed principle, and it's what proves your abstractions weren't accidental.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — O(1), pluggable, thread-safe — they pick your data structures *and* your seams.
2. **Three roles**: Strategy interface (`EvictionPolicy`), concrete strategies (LRU/LFU/FIFO), context (`Cache`).
3. **Interface first** — the policy speaks **only in keys**, so storage and eviction decouple.
4. **Leaves first** — pick the right containers (`unordered_map` for storage); model the algorithm where it lives.
5. **The key trick**: **hashmap + doubly-linked-list** — map locates in O(1), list orders + picks the victim in O(1); `splice` reorders without invalidating iterators.
6. **Context coordinates, strategy decides** — `Cache` asks "who's the victim?"; it never computes recency.
7. **Thread-safety**: one mutex first (`get` is a writer!), then name contention → sharding.
8. **Follow-ups = a new subclass or composition** (LFU bucket policy, TTL field, shard wrapper, backing-store strategy), never a core edit.

Follow that skeleton on any "design a cache / bounded store with a policy" LLD and the map+list core plus the Strategy seam fall out almost mechanically.
