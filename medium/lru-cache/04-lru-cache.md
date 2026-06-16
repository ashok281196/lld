# LRU Cache — LLD Problem Statement

**Difficulty:** Medium
**Language:** C++
**Pattern focus:** DS design + Strategy (pluggable eviction) + thread safety

---

## Context
Design an in-memory key-value cache with a fixed capacity and an eviction policy, suitable for embedding in a service.

## Functional Requirements
- `get(key)` returns the value if present, else a miss.
- `put(key, value)` inserts/updates; if at capacity, **evict** per the policy before inserting.
- Base policy: **LRU** — evict the least-recently-used entry.
- Both `get` and `put` must be **O(1)** average time.

## Non-Functional / Constraints
- The **eviction policy must be pluggable** (LRU base; LFU / FIFO as drop-ins via an `EvictionPolicy` interface) — this generalization is the point of the exercise at SDE-3, not just coding LRU.
- **Thread-safe** for concurrent `get`/`put`.
- Generic over key and value types.

## Expected Public Interface
```cpp
template <typename K, typename V>
class EvictionPolicy {                // Strategy
public:
    virtual void keyAccessed(const K& key) = 0;
    virtual K    evictKey() = 0;       // returns the key to remove
    virtual void keyAdded(const K& key) = 0;
    virtual ~EvictionPolicy() = default;
};

template <typename K, typename V>
class Cache {
public:
    Cache(size_t capacity, std::unique_ptr<EvictionPolicy<K,V>> policy);
    std::optional<V> get(const K& key);
    void put(const K& key, const V& value);
};
```

## What the Interviewer Is Really Testing
- The **hashmap + doubly-linked-list** mechanism for O(1) LRU (be ready to justify why both).
- Cleanly **decoupling the policy** from the storage so LFU/FIFO swap in.
- The **thread-safety** answer (single mutex first; then discuss sharding / striped locks for contention).

## Follow-Up Questions to Expect
1. Swap to **LFU** (frequency buckets) without changing the cache storage.
2. **TTL / expiry** per entry alongside capacity eviction.
3. Reduce lock contention: **sharded** cache (partition by key hash).
4. Make it **write-through / write-back** to a backing store.

## Your Task
1. Assumptions + interface, then the map+list internals.
2. Implement LRU behind the `EvictionPolicy` interface.
3. Add the mutex, then attempt the LFU swap to prove the abstraction holds.
