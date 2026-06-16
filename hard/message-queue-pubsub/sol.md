# In-Memory Message Queue / Pub-Sub — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **Observer-at-scale + producer-consumer** design feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design:

1. *"Delivery should not block producers — **decouple** publish from consume."* → `publish()` must **not** call subscribers inline. It enqueues and returns. This is the **producer-consumer** split: a buffer sits between the two sides.
2. *"**Thread-safe** under many concurrent producers and consumers."* → Every shared structure (the topic registry, each topic's queue, each consumer's offset) needs a defined locking story. Concurrency isn't a follow-up here; it's the body of the answer.
3. *"**Observer** at the core; messages pushed to consumers."* → Subscribers are observers. But naive Observer (publish loops over subscribers and calls them) **violates constraint #1**. The twist being tested is Observer *plus* an async dispatch layer.
4. *"Each consumer tracks its **offset**… and can **replay** from an offset."* → The queue is really an append-only **log**; consumers are cursors into it, not a destructive `pop`. Replay = move the cursor back.
5. *"**Backpressure / bounded buffers**… block, drop, or buffer with a cap — **state your policy**."* → The interviewer is explicitly asking you to *choose and justify*. Picking one and naming the trade-off is the seniority signal.
6. *"At-least-once or at-most-once — pick and justify."* → Same: a decision, said out loud.

> **Thinking habit:** when the prompt says "don't block producers" and "thread-safe," it's naming a *producer-consumer queue with a dispatch thread*, not a simple Observer loop. Build the async seam first; everything else hangs off it.

---

## Step 1 — State assumptions, then find the nouns → these become your classes

Before classes, lock the two decisions the prompt forced on us:

- **Delivery semantics: at-most-once** for the baseline. Dispatch advances a consumer's offset *after* a successful `onMessage` call; if the consumer throws, we log and move on (no redelivery yet). At-least-once with acks is the Step 9 follow-up.
- **Backpressure: bounded buffer per topic with a configurable cap; on overflow, block the producer** (the slow-consumer back-pressures the fast producer). We note "drop-oldest" as the alternative for telemetry-style workloads. Bounded = no unbounded memory growth, exactly as required.

Now circle the nouns: *message, topic, log, offset, producer, consumer/subscriber, group, broker, dispatcher, buffer.*

| Class | Owns | Why it exists |
|-------|------|---------------|
| `Message` | payload + metadata (id, timestamp) | the unit that flows through the system |
| `Subscriber` (interface) | `onMessage(topic, msg)` | the **Observer** — receives delivered messages |
| `Topic` | the append-only **log**, per-consumer **offsets**, the topic mutex/CV | one channel; the unit of thread-safety and replay |
| `MessageBroker` | topic registry, the **dispatch thread pool** | the public façade; routes publish/subscribe, owns async delivery |
| `ConsumerGroup` (follow-up) | members + round-robin cursor | competing-consumer load balancing within a group |

> **Thinking habit:** Observer-at-scale = one **Subject/registry** (`Topic`), N **Observers** (`Subscriber`), and a **dispatch mechanism** between them. The dispatch layer is the class the naive answer forgets — spot it now.

---

## Step 2 — Pin the public interface (the contract)

Given to us — lock it before internals. The shape already encodes the key decisions:

```cpp
#include <string>
#include <memory>

struct Message {                       // richer than a bare string
    std::string payload;
    uint64_t    id        = 0;         // monotonic offset within its topic
    long long   timestamp = 0;
};

class Subscriber {                     // Observer
public:
    virtual void onMessage(const std::string& topic, const Message& msg) = 0;
    virtual ~Subscriber() = default;
};

class MessageBroker {
public:
    void createTopic(const std::string& topic);
    void publish(const std::string& topic, const Message& msg);                 // non-blocking
    void subscribe(const std::string& topic, std::shared_ptr<Subscriber> sub,
                   const std::string& consumerId, const std::string& groupId = "");
    void resetOffset(const std::string& topic, const std::string& consumerId, size_t offset);
};
```

Decisions baked in here:
- **`publish` is documented non-blocking** — it appends to the log and signals the dispatcher; it never calls `onMessage`. (Under our backpressure policy it *may* block when the buffer is full — that's deliberate back-pressure, not delivery work.)
- **`subscribe` takes a `consumerId`** — offsets are per-consumer, so we need a stable key. (The problem's `resetOffset` already references `consumerId`, so the interface implies it.)
- **`shared_ptr<Subscriber>`** — the broker doesn't own the consumer's lifetime exclusively; a consumer may outlive or be shared across topics. Shared ownership avoids dangling during async dispatch.

> **Thinking habit:** the interface is a promise. "`publish` non-blocking" is a *promise about threading*, not about the signature — write that contract down so internals can't quietly break it.

---

## Step 3 — Model the leaves: `Message` and `Subscriber`

Bottom-up: things with no dependencies first.

`Message` is a value type. Give it an `id` that doubles as its **offset** in the topic log — then "offset N" and "message with id N" are the same thing, and replay becomes trivial.

```cpp
struct Message {
    std::string payload;
    uint64_t    id        = 0;   // == index in the topic's log
    long long   timestamp = 0;
};
```

`Subscriber` is the Observer interface, exactly as given. Concrete consumers implement it:

```cpp
class Subscriber {
public:
    virtual void onMessage(const std::string& topic, const Message& msg) = 0;
    virtual ~Subscriber() = default;
};

// A trivial concrete observer for testing.
class PrintingSubscriber : public Subscriber {
public:
    explicit PrintingSubscriber(std::string name) : name_(std::move(name)) {}
    void onMessage(const std::string& topic, const Message& msg) override {
        std::cout << "[" << name_ << "] " << topic << "#" << msg.id
                  << " -> " << msg.payload << "\n";
    }
private:
    std::string name_;
};
```

> **Thinking habit:** make the message id *be* the offset. One field serving two roles (identity + position) collapses replay logic into a single integer comparison.

---

## Step 4 — The key insight: the log is append-only, consumers are cursors

This is the heart of the problem. Spend real thought here.

**Naive idea:** a `std::queue` per topic; deliver = `pop()`. This breaks two requirements at once — you can't **replay** (the message is gone), and **broadcast** is impossible (the first consumer consumes it before the others see it).

**The log + cursor model.** A topic is an **append-only vector** (a log). Each consumer holds an **offset** = the index of the next message it should read. Delivery never removes from the log; it advances a cursor.

```
Topic "orders" log:   [ m0 ][ m1 ][ m2 ][ m3 ][ m4 ]   <- append at tail
                                         ▲              ▲
                          consumerB.offset=2       consumerA.offset=5 (caught up)
```

- **Broadcast** falls out for free: every consumer has its own offset, so every consumer independently walks the whole log. No message is "used up."
- **Replay** = `resetOffset(consumerId, k)` sets that consumer's cursor back to `k`. Next dispatch re-delivers from `k`. One assignment.
- **Backpressure / bounded buffer**: the log can't grow forever. We cap it. When full and a slow consumer's offset still points into the region we'd overwrite, the producer **blocks** until the slow consumer drains enough (our stated policy). Alternative: drop the oldest and bump lagging offsets forward (at-most-once, lossy) — name it as the telemetry-friendly choice.

Why this is *the* insight: it unifies broadcast, replay, and bounded buffering under one structure. The queue isn't a queue — it's a log with per-reader cursors. That's the Kafka mental model the problem is fishing for.

> **Thinking habit:** when you see "replay" + "every subscriber gets every message," stop reaching for `queue::pop`. The structure is an **append-only log with per-consumer cursors** — destructive reads can't give you either property.

---

## Step 5 — Build `Topic`: the log, the offsets, and its own lock

`Topic` is the unit of thread-safety. Each topic carries its own mutex and condition variable, so traffic on one topic never contends with another (lock granularity = per topic, not one global lock).

```cpp
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>

struct Consumer {
    std::string            id;
    std::string            groupId;     // "" => standalone broadcast consumer
    std::shared_ptr<Subscriber> sub;
    size_t                 offset = 0;  // next index to deliver
};

class Topic {
public:
    explicit Topic(std::string name, size_t capacity = 1024)
        : name_(std::move(name)), capacity_(capacity) {}

    // Producer side. Blocks if the log is full AND the slowest consumer
    // hasn't drained past the head we'd need to evict (backpressure policy).
    void append(const Message& msg) {
        std::unique_lock<std::mutex> lk(mtx_);
        notFull_.wait(lk, [&]{ return log_.size() - minOffset() < capacity_; });
        Message stamped = msg;
        stamped.id = baseOffset_ + log_.size();   // id == absolute offset
        log_.push_back(std::move(stamped));
        notEmpty_.notify_all();                    // wake the dispatcher
    }

    // Register a consumer; new consumers start at the current tail.
    void addConsumer(const std::string& consumerId, const std::string& groupId,
                     std::shared_ptr<Subscriber> sub) {
        std::lock_guard<std::mutex> lk(mtx_);
        consumers_[consumerId] = Consumer{consumerId, groupId, std::move(sub),
                                          baseOffset_ + log_.size()};
    }

    void resetOffset(const std::string& consumerId, size_t offset) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = consumers_.find(consumerId);
        if (it == consumers_.end())
            throw std::invalid_argument("unknown consumer: " + consumerId);
        it->second.offset = offset;                // replay: move cursor back
        notEmpty_.notify_all();
    }

    // Dispatcher side: pull the next (consumer, message) that is ready, if any.
    // Returns false if nothing is pending right now.
    bool nextPending(Consumer*& who, Message& out) {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& [id, c] : consumers_) {
            size_t idx = c.offset - baseOffset_;
            if (idx < log_.size()) { who = &c; out = log_[idx]; return true; }
        }
        return false;
    }

    // Called by the dispatcher AFTER a successful onMessage (at-most-once commit).
    void commit(const std::string& consumerId) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = consumers_.find(consumerId);
        if (it != consumers_.end()) ++it->second.offset;
        notFull_.notify_all();                     // a consumer advanced -> maybe room now
    }

    std::mutex& mutex() { return mtx_; }
    std::condition_variable& notEmpty() { return notEmpty_; }

private:
    // Smallest offset any consumer still needs — the eviction floor.
    size_t minOffset() const {
        if (consumers_.empty()) return baseOffset_ + log_.size();  // no readers: free to grow toward cap
        size_t m = SIZE_MAX;
        for (auto& [id, c] : consumers_) m = std::min(m, c.offset);
        return m;
    }

    std::string name_;
    size_t      capacity_;
    size_t      baseOffset_ = 0;                   // offset of log_[0] (advances if we evict)
    std::vector<Message> log_;
    std::unordered_map<std::string, Consumer> consumers_;
    std::mutex mtx_;
    std::condition_variable notEmpty_;             // dispatcher waits on this
    std::condition_variable notFull_;              // producers wait on this (backpressure)
};
```

Call out three design wins in an interview:
- **Per-topic lock** — concurrency scales with topic count; no single global bottleneck.
- **`baseOffset_`** lets the drop-oldest variant evict from the front without renumbering message ids — absolute offsets stay stable forever (critical for replay correctness).
- **Two condition variables** (`notEmpty_`, `notFull_`) are the classic bounded-buffer pair: producers wait on `notFull_`, the dispatcher waits on `notEmpty_`.

> **Thinking habit:** pick your **lock granularity** deliberately. "One mutex per topic" is a sentence that tells the interviewer you thought about contention — a single `std::mutex broker_mtx_` around everything is the answer that doesn't.

---

## Step 6 — The `MessageBroker`: façade + dispatch thread pool

The broker is the public face. Its `publish`/`subscribe` are thin routing; the real engine is a **pool of dispatch threads** that drain topics and call `onMessage` off the producer's thread. *That* is what makes `publish` non-blocking.

```cpp
#include <thread>
#include <atomic>

class MessageBroker {
public:
    explicit MessageBroker(unsigned dispatchers = std::thread::hardware_concurrency()) {
        running_ = true;
        for (unsigned i = 0; i < std::max(1u, dispatchers); ++i)
            pool_.emplace_back([this]{ dispatchLoop(); });
    }

    ~MessageBroker() {
        running_ = false;
        // wake every dispatcher so it can observe running_ == false and exit.
        { std::lock_guard<std::mutex> lk(reg_mtx_);
          for (auto& [n, t] : topics_) t->notEmpty().notify_all(); }
        for (auto& th : pool_) if (th.joinable()) th.join();
    }

    void createTopic(const std::string& topic) {
        std::lock_guard<std::mutex> lk(reg_mtx_);
        topics_.emplace(topic, std::make_unique<Topic>(topic));
    }

    void publish(const std::string& topic, const Message& msg) {
        Topic* t = find(topic);                    // brief reg lock, then release
        t->append(msg);                            // non-blocking (except backpressure)
    }

    void subscribe(const std::string& topic, std::shared_ptr<Subscriber> sub,
                   const std::string& consumerId, const std::string& groupId = "") {
        find(topic)->addConsumer(consumerId, groupId, std::move(sub));
    }

    void resetOffset(const std::string& topic, const std::string& consumerId, size_t offset) {
        find(topic)->resetOffset(consumerId, offset);
    }

private:
    Topic* find(const std::string& topic) {
        std::lock_guard<std::mutex> lk(reg_mtx_);
        auto it = topics_.find(topic);
        if (it == topics_.end()) throw std::invalid_argument("no such topic: " + topic);
        return it->second.get();
    }

    // Each dispatcher repeatedly: find a pending delivery, call onMessage, commit.
    void dispatchLoop() {
        while (running_) {
            bool didWork = false;
            std::vector<Topic*> snapshot;
            { std::lock_guard<std::mutex> lk(reg_mtx_);
              for (auto& [n, t] : topics_) snapshot.push_back(t.get()); }

            for (Topic* t : snapshot) {
                Consumer* who = nullptr;
                Message   msg;
                if (t->nextPending(who, msg)) {
                    std::string topicName, consumerId;
                    std::shared_ptr<Subscriber> sub;
                    { // copy out what we need; don't hold the topic lock during onMessage
                      std::lock_guard<std::mutex> lk(t->mutex());
                      consumerId = who->id; sub = who->sub;
                    }
                    try { sub->onMessage(/*topic*/ msg.payload.empty() ? "" : "", msg); }
                    catch (...) { /* at-most-once: log + skip, still commit */ }
                    t->commit(consumerId);         // advance cursor
                    didWork = true;
                }
            }
            if (!didWork) std::this_thread::yield(); // production: wait on a CV instead of spin
        }
    }

    std::unordered_map<std::string, std::unique_ptr<Topic>> topics_;
    std::mutex            reg_mtx_;                 // guards the topic registry only
    std::vector<std::thread> pool_;
    std::atomic<bool>     running_{false};
};
```

The critical correctness rule, stated explicitly: **never hold the topic lock while calling `onMessage`.** A subscriber callback is user code — it may be slow, may block, may even re-enter the broker. Copy out the `shared_ptr<Subscriber>` and the message under the lock, *release the lock*, then deliver. Holding the lock across user code is how real systems deadlock.

> **Thinking habit:** identify the "user code" boundary in any callback system and **drop your locks before crossing it.** What runs inside the lock must be code *you* control and that finishes fast.

---

## Step 7 — Backpressure & delivery semantics, said out loud

The prompt explicitly grades you on *choosing and justifying*. Two sentences each:

**Backpressure — bounded log, block the producer.** Capacity is per topic. `append` waits on `notFull_` until the slowest consumer has advanced enough that we're under cap. This applies real back-pressure: a slow consumer eventually throttles a fast producer instead of exhausting memory.
- *Alternative — drop-oldest:* evict `log_[0]`, bump `baseOffset_`, and shove any lagging cursor forward to the new floor. Bounded memory, but lossy for slow consumers. Right for metrics/telemetry where freshness beats completeness; wrong for orders/payments.

**Delivery — at-most-once (baseline).** We `commit` (advance the offset) after `onMessage` returns. If the broker crashes between delivery and commit, the message may be redelivered; if the consumer throws, we skip it — so effectively at-most-once for crashes, and we never block the whole topic on one bad consumer.
- *Upgrade — at-least-once:* don't auto-commit. Hand the consumer an `ack(offset)`; redeliver if no ack within a timeout. Now a crash *replays* rather than loses — the durability/ack follow-up.

> **Thinking habit:** when a prompt says "pick and justify," the *justification* is the answer. State the policy, then one concrete workload where the alternative wins. That contrast is what reads as senior.

---

## Step 8 — Prove it with a driver

Always show a `main` that exercises broadcast, a replay, and clean shutdown. It doubles as your test.

```cpp
#include <iostream>

int main() {
    MessageBroker broker(/*dispatchers=*/2);
    broker.createTopic("orders");

    auto alice = std::make_shared<PrintingSubscriber>("alice");
    auto bob   = std::make_shared<PrintingSubscriber>("bob");

    // Broadcast: both standalone consumers get every message.
    broker.subscribe("orders", alice, "alice");
    broker.subscribe("orders", bob,   "bob");

    for (int i = 0; i < 3; ++i)
        broker.publish("orders", Message{"order-" + std::to_string(i)});

    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let dispatch drain

    // Replay: rewind alice to offset 0; she re-receives the whole log.
    std::cout << "--- alice replays from 0 ---\n";
    broker.resetOffset("orders", "alice", 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // resetOffset on an unknown consumer is rejected, not crashed.
    try { broker.resetOffset("orders", "ghost", 0); }
    catch (const std::exception& e) { std::cout << "Rejected: " << e.what() << "\n"; }

    return 0;   // ~MessageBroker stops and joins the dispatch pool cleanly
}
```

Expected shape of output: each of the three orders printed once for `alice` and once for `bob` (broadcast), then all three printed again for `alice` only (replay), then the `Rejected` line.

> **Thinking habit:** the driver must hit broadcast (the Observer fan-out), replay (the cursor rewind), *and* an error path — that proves the three properties the structure was designed for, in one screen.

---

## Step 9 — Talk through the remaining follow-ups

Show the seams are already there:

1. **Consumer groups (competing consumers).** Each `Consumer` already carries a `groupId`. Add a per-topic `std::unordered_map<groupId, GroupState>` holding **one shared offset** and a round-robin cursor. In `nextPending`, if a consumer is in a group, pick the message at the *group's* offset and hand it to exactly one member (round-robin), then advance the group offset on commit. Broadcast = group of one each; partitioned load-balancing = hashing message keys to members. **Same log, different cursor ownership** — no structural change.

2. **At-least-once with acks + redelivery.** Stop auto-committing in `dispatchLoop`. Track in-flight `(consumerId, offset, sentAt)`; expose `ack(topic, consumerId, offset)` that commits. A reaper thread redelivers entries past a timeout. The log already supports re-reading any offset, so redelivery is just *don't advance the cursor yet*.

3. **Persistence / durability.** The append-only log *is* a write-ahead log. Back each `Topic` with a segment file: `append` fsyncs the record before signalling; on restart, replay segments to rebuild `log_` and load committed offsets from an offsets file. The in-memory design maps 1:1 onto Kafka's on-disk segments — that's not a coincidence, it's why we chose a log.

4. **Ordering guarantees.** Within a single topic our `std::vector` log gives **total order**, and a single consumer reads it in order. Across consumer-group members, order holds *per partition* only: split a topic into P partitions (P logs), hash key → partition, one consumer per partition per group. Global ordering across partitions is sacrificed for parallelism — the same trade-off Kafka makes, stated explicitly.

> **Thinking habit:** good distributed-systems LLD ends by mapping your in-memory toy onto the real system it mimics (here, Kafka's log + offsets + partitions). Naming that correspondence proves your abstractions were principled, not improvised.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — "don't block producers" + "thread-safe" = a producer-consumer queue with a dispatch thread, not a naive Observer loop.
2. **State assumptions up front** — delivery semantics (at-most-once) and backpressure (bounded + block) are decisions to *declare*, not discover.
3. **Three roles**: Subject/registry (`Topic`), Observers (`Subscriber`), and the **dispatch layer** the naive answer forgets.
4. **Interface first** — and `publish` non-blocking is a *threading* promise, not just a signature.
5. **The key structure**: an **append-only log with per-consumer cursors** — it gives broadcast, replay, and bounded buffering all at once.
6. **Lock granularity per topic**; two condition variables for the bounded buffer; **drop the lock before calling user code** (`onMessage`).
7. **Dispatch thread pool** decouples publish from delivery; commit-after-deliver = at-most-once.
8. **Follow-ups = new cursor ownership** (groups), **deferred commit** (at-least-once), **a backing file** (durability), **partitions** (ordering) — never a rewrite.

Follow that skeleton on any "design a queue / event bus / notification fan-out" LLD and the Observer-at-scale design falls out almost mechanically.
