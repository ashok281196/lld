# In-Memory Message Queue / Pub-Sub — LLD Problem Statement

**Difficulty:** Hard
**Language:** C++
**Pattern focus:** Observer at scale + producer-consumer concurrency + offsets/backpressure

---

## Context
Design an in-memory publish-subscribe / message-queue system (a mini-Kafka): producers publish messages to topics, consumers subscribe and receive them, with per-consumer progress tracking.

## Functional Requirements
- **Topics**: named channels. Producers **publish** messages to a topic.
- **Consumers** subscribe to a topic and **receive** messages, either:
  - **broadcast** (every subscriber gets every message), and/or
  - **consumer groups** (each message delivered to exactly one consumer in a group).
- Each consumer tracks its **offset** (position) so it resumes correctly and can **replay** from an offset.
- Delivery should not block producers — decouple publish from consume.

## Non-Functional / Constraints
- **Thread-safe** under many concurrent producers and consumers.
- **Observer** (subscribers) at the core; messages pushed to (or pulled by) consumers.
- **Backpressure / bounded buffers**: handle a slow consumer without unbounded memory growth (block, drop, or buffer with a cap — state your policy).
- At-least-once or at-most-once delivery — pick and justify.

## Expected Public Interface
```cpp
using Message = std::string;          // or a struct with payload + metadata

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
                   const std::string& groupId = "");                            // group => competing consumers
    void resetOffset(const std::string& topic, const std::string& consumerId, size_t offset);  // replay
};
```

## What the Interviewer Is Really Testing
- **Observer** done at scale with a **producer-consumer queue** decoupling publish and delivery.
- **Per-consumer offsets** and the ability to **replay**.
- A concrete **concurrency** design (per-topic queue + condition variables; thread pool for dispatch).
- A stated **backpressure** policy for slow consumers — the detail that signals seniority.

## Follow-Up Questions to Expect
1. **Consumer groups** with partition-style load balancing across consumers.
2. **At-least-once** delivery with acknowledgements and redelivery on timeout.
3. **Persistence / durability** (discuss how the in-memory log would extend to disk).
4. **Ordering guarantees** within a partition vs across the topic.

## Your Task
1. Assumptions (delivery semantics, backpressure policy) + interface, then `Topic`, `MessageBroker`, `Subscriber`.
2. Implement broadcast delivery with per-consumer offsets and a bounded buffer.
3. Add a dispatch thread pool + condition variables; attempt consumer groups as the follow-up.
