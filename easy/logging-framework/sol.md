# Logging Framework — Step-by-Step Solution Thinking

This document walks you from a blank page to a complete C++ solution, explaining **how to think** at every step. The goal is the reasoning that makes the **Singleton + Strategy + Chain of Responsibility** trio feel inevitable rather than memorized.

---

## Step 0 — Read the problem like an interviewer wrote it

Mine the hard constraints first — they decide the design. This problem unusually *names its own patterns*, so the work is matching each requirement to the role it plays:

1. *"A single global access point… configurable once at startup."* → **Singleton**. One shared `Logger`, private constructor, non-copyable. The interviewer wants to see you get the C++11 thread-safe singleton right.
2. *"Pluggable output sinks (console/file/network), switchable without touching call sites."* → **Strategy**. An `Appender` interface; concrete sinks are interchangeable. Call sites only ever say `log(...)`.
3. *"Levels with ordering; messages below threshold are dropped… model as a Chain of Responsibility."* → **Chain of Responsibility**. Each level handler decides "is this loud enough for me?" and passes along.
4. *"Thread-safe writes: concurrent threads must not interleave a single message."* → A **mutex** around the append. Say this out loud; it's an explicit grading criterion.
5. *"A `Formatter` abstraction controls layout."* → A second tiny Strategy, orthogonal to the sink. Bonus points.

> **Thinking habit:** when a prompt hands you the pattern names, don't celebrate — *map each requirement to the role it fills*. The grade is in showing why each pattern is the right tool, not in reciting it.

---

## Step 1 — Find the nouns → these become your classes

Circle the nouns: *logger, level, message, sink/appender, console, file, formatter, threshold, thread.*

| Class | Owns | Why it exists |
|-------|------|---------------|
| `LogLevel` (enum) | the ordered severities | comparable threshold unit |
| `LogMessage` | level, text, timestamp, thread id | one immutable record to render |
| `Formatter` (interface) | how a message becomes a string | swappable layout (Strategy) |
| `Appender` (interface) | where a rendered message goes | swappable sink (Strategy) |
| `ConsoleAppender` / `FileAppender` | the actual write target | concrete sinks |
| `LevelHandler` (chain node) | a threshold + a `next` link | accept-or-pass level filtering (CoR) |
| `Logger` | level, appenders, formatter, mutex | the **Singleton** façade everyone calls |

> **Thinking habit:** Strategy and Singleton each have a tell. Strategy = "interface + N interchangeable implementations." Singleton = "one global instance, private ctor." Spot the tells and the class list writes itself.

---

## Step 2 — Pin the public interface (the contract)

The statement hands us most of it — lock it before internals:

```cpp
enum class LogLevel { DEBUG, INFO, WARN, ERROR };   // declared in increasing severity

class Logger {
public:
    static Logger& getInstance();                    // Singleton access point

    void log(LogLevel level, const std::string& msg);
    void setLevel(LogLevel level);                   // configure threshold
    void addAppender(std::unique_ptr<Appender>);     // plug in a sink
    void setFormatter(std::unique_ptr<Formatter>);   // bonus: choose layout

    Logger(const Logger&)            = delete;        // non-copyable
    Logger& operator=(const Logger&) = delete;        // non-movable too

private:
    Logger();                                         // private — only getInstance builds it
};
```

Two decisions baked in here:
- **`addAppender` takes a `std::unique_ptr`** — the logger *owns* its sinks, and ownership transfer is explicit at the call site (`std::move`).
- **Copy/move deleted.** A second `Logger` would defeat "single global access point," so the compiler forbids it.

> **Thinking habit:** the enum declaration order *is* the ordering contract. Because `DEBUG=0 < ERROR=3`, a plain `<` on the underlying value is your threshold test — no comparison table needed.

---

## Step 3 — Model the leaves: `LogLevel`, `LogMessage`, `Formatter`

Bottom-up: dependency-free types first.

```cpp
#include <string>
#include <chrono>
#include <thread>
#include <sstream>

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

inline const char* toString(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?";
}

struct LogMessage {
    LogLevel    level;
    std::string text;
    std::chrono::system_clock::time_point time = std::chrono::system_clock::now();
    std::thread::id threadId = std::this_thread::get_id();
};
```

`Formatter` is a one-method Strategy: turn a `LogMessage` into the final string. A default implementation renders `[LEVEL] text`; richer ones add timestamp/thread.

```cpp
class Formatter {
public:
    virtual ~Formatter() = default;
    virtual std::string format(const LogMessage& m) const = 0;
};

class SimpleFormatter : public Formatter {
public:
    std::string format(const LogMessage& m) const override {
        std::ostringstream os;
        os << "[" << toString(m.level) << "] " << m.text;
        return os.str();
    }
};
```

> **Thinking habit:** make `LogMessage` carry *raw facts* (level, text, time, thread), not pre-rendered text. Rendering is the `Formatter`'s job — keep data and presentation apart so either can change alone.

---

## Step 4 — The key insight: three patterns, three independent axes

The trap is treating this as one big class. It's really **three orthogonal decisions**, each owned by a different pattern:

```
              Logger (Singleton — the one façade)
                 │
   "Is it loud   │   log(level, msg)
    enough?"     ▼
            LevelHandler chain  ──►  Chain of Responsibility   (the AXIS: severity)
                 │ passes only messages >= threshold
                 ▼
            Formatter            ──►  Strategy                  (the AXIS: layout)
                 │ renders LogMessage -> string
                 ▼
            Appender(s)          ──►  Strategy                  (the AXIS: destination)
                 │ console / file / network
                 ▼              (mutex guards this write — no interleaving)
```

- **Singleton** answers *who do I call?* — one global `Logger`.
- **Chain of Responsibility** answers *should this message survive?* — level filtering.
- **Strategy** answers *how is it rendered?* (`Formatter`) and *where does it go?* (`Appender`).

Each axis varies without disturbing the others. That separation is the whole design.

**The CoR detail worth getting right.** A handler holds a threshold and a `next` pointer. If the message's level meets the handler's bar, it acts (or forwards to the sinks); otherwise it passes down the chain. For a simple level filter the chain is almost degenerate — but modeling it as a chain is what lets you later insert *per-level routing* (e.g. ERROR also emails) without rewriting the logger.

```cpp
class LevelHandler {
public:
    explicit LevelHandler(LogLevel threshold) : threshold_(threshold) {}
    virtual ~LevelHandler() = default;

    void setNext(LevelHandler* next) { next_ = next; }

    // Walk the chain; a handler that "owns" this severity dispatches it.
    void handle(const LogMessage& m, const std::vector<std::unique_ptr<Appender>>& sinks,
                const Formatter& fmt) {
        if (m.level >= threshold_) {          // enum order makes >= the threshold test
            dispatch(m, sinks, fmt);
        } else if (next_) {
            next_->handle(m, sinks, fmt);     // not for me — pass it along
        }
        // dropped silently if no handler accepts (below threshold)
    }

protected:
    virtual void dispatch(const LogMessage& m,
                          const std::vector<std::unique_ptr<Appender>>& sinks,
                          const Formatter& fmt) {
        std::string line = fmt.format(m);
        for (auto& s : sinks) s->append({m.level, line});
    }

private:
    LogLevel      threshold_;
    LevelHandler* next_ = nullptr;
};
```

> ⚠️ Note `m.level >= threshold_` relies on `enum class LogLevel` underlying ints being **in severity order**. Comparing scoped enums needs an explicit cast or a helper — define `inline bool operator>=(LogLevel a, LogLevel b){ return static_cast<int>(a) >= static_cast<int>(b); }` once and the chain reads cleanly. State this assumption to the interviewer.

> **Thinking habit:** when a problem stacks multiple patterns, find the *independent axes of change* (who/whether/how/where). One pattern per axis, and they compose without entangling.

---

## Step 5 — Model the sinks: the `Appender` Strategy

Each sink is a destination. They share the one-method interface and never know about levels or formatting — they just write the finished string.

```cpp
#include <iostream>
#include <fstream>

class Appender {
public:
    virtual ~Appender() = default;
    virtual void append(const LogMessage& m) = 0;   // m.text is already rendered
};

class ConsoleAppender : public Appender {
public:
    void append(const LogMessage& m) override {
        std::ostream& out = (m.level >= LogLevel::WARN) ? std::cerr : std::cout;
        out << m.text << "\n";
    }
};

class FileAppender : public Appender {
public:
    explicit FileAppender(const std::string& path) : file_(path, std::ios::app) {}
    void append(const LogMessage& m) override { file_ << m.text << "\n"; }
private:
    std::ofstream file_;
};
```

A `NetworkAppender` later is just another subclass — no caller changes, no `Logger` changes. That's the Strategy payoff the prompt asked for ("switchable without touching call sites").

> **Thinking habit:** keep each Strategy implementation ignorant of the others' concerns. The sink writes bytes; it must not re-decide level or format. Single responsibility keeps the swap clean.

---

## Step 6 — The Singleton: `Logger` ties it together

The C++11 **function-local static** (Meyers singleton) gives you lazy, thread-safe, one-time initialization for free — the standard guarantees the init is atomic.

```cpp
#include <vector>
#include <memory>
#include <mutex>

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;   // C++11: initialized exactly once, thread-safe
        return instance;
    }

    void setLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        chain_ = LevelHandler(level);     // rebuild the (single-node) chain at new threshold
    }

    void addAppender(std::unique_ptr<Appender> sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        sinks_.push_back(std::move(sink));
    }

    void setFormatter(std::unique_ptr<Formatter> fmt) {
        std::lock_guard<std::mutex> lock(mutex_);
        formatter_ = std::move(fmt);
    }

    void log(LogLevel level, const std::string& msg) {
        // Build the record outside the lock; only the dispatch is critical.
        LogMessage record{level, msg};
        std::lock_guard<std::mutex> lock(mutex_);   // <-- no two messages interleave
        chain_.handle(record, sinks_, *formatter_);
    }

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger()
        : chain_(LogLevel::INFO),                       // default threshold
          formatter_(std::make_unique<SimpleFormatter>()) {}

    std::mutex                                mutex_;
    LevelHandler                              chain_;
    std::vector<std::unique_ptr<Appender>>    sinks_;
    std::unique_ptr<Formatter>                formatter_;
};
```

Three things to call out in an interview:
- **`getInstance` returns a reference**, never a pointer — callers can't `delete` it or test it for null.
- **The mutex wraps the dispatch**, so the whole "render + write to every sink" for one message is atomic → no interleaving (the explicit requirement).
- **Config setters also lock**, because a thread might reconfigure while another logs.

> **Thinking habit:** prefer the Meyers singleton over the double-checked-locking dance. The language already solved one-time init; don't hand-roll it and risk a data race.

---

## Step 7 — Prove it with a tiny driver

Show configuration, a dropped message (below threshold), a delivered one, and concurrency.

```cpp
#include <thread>

int main() {
    Logger& log = Logger::getInstance();
    log.setLevel(LogLevel::INFO);                         // DEBUG will be dropped
    log.addAppender(std::make_unique<ConsoleAppender>());
    log.addAppender(std::make_unique<FileAppender>("app.log"));

    log.log(LogLevel::DEBUG, "starting up");              // below INFO -> silently dropped
    log.log(LogLevel::INFO,  "service ready");            // printed + written
    log.log(LogLevel::ERROR, "disk full");                // goes to std::cerr + file

    // Concurrent writers must not interleave a single line.
    std::thread a([&]{ log.log(LogLevel::WARN, "from thread A"); });
    std::thread b([&]{ log.log(LogLevel::WARN, "from thread B"); });
    a.join();
    b.join();
    return 0;
}
```

> **Thinking habit:** a driver that hits a *dropped* level, a *delivered* level, and *two threads* proves all three patterns plus the mutex in fifteen lines. Pick inputs that each exercise one requirement.

---

## Step 8 — Talk through the follow-ups (don't necessarily code them all)

1. **Async logging.** The mutex serializes writers, but a slow `FileAppender` now blocks every caller. Decouple producing from writing with a **producer-consumer queue**: `log()` renders the line and pushes it onto a thread-safe `std::queue` guarded by a mutex + `std::condition_variable`; a single background thread pops and appends. Sketch:

   ```cpp
   class AsyncLogger {
       std::queue<LogMessage> q_;
       std::mutex m_;
       std::condition_variable cv_;
       std::thread worker_;
       bool done_ = false;
       // log(): { lock; q_.push(rec); } cv_.notify_one();
       // worker: wait on cv_, pop, dispatch to sinks; drain on shutdown.
   };
   ```

   The hard parts to mention: **bounded queue** (back-pressure or drop policy when the producer outruns the sink), and a **clean flush on shutdown** so you don't lose buffered lines. Only the *delivery* changed — Singleton/Strategy/CoR are untouched.

2. **Log rotation** by size or date. Wrap `FileAppender`: track bytes written; when it crosses a cap, close, rename to `app.log.1`, reopen fresh. A `RotatingFileAppender` is just another Strategy — no other class moves.

3. **Per-module loggers** with independent levels and a parent/child hierarchy. Give each named `Logger` its own threshold but let it **fall back to a parent** for sinks (mirroring log4j). The chain idea generalizes: an unhandled level walks up to the parent's chain. Now the CoR you "over-built" in Step 4 pays off.

> **Thinking habit:** good follow-up answers show a new requirement = a new subclass or a new queue stage, *not* an edit to the core `log()`. That's open/closed, and it proves the axes you separated were the right ones.

---

## Recap — the reasoning skeleton you can reuse

1. **Mine the constraints** — the prompt named the patterns; map each requirement to its role (who/whether/how/where).
2. **Nouns → classes**, spotting the Singleton tell (one global, private ctor) and the Strategy tell (interface + N impls).
3. **Interface first** — Singleton returns a *reference*, owns sinks via `unique_ptr`, deletes copy/move.
4. **Leaves first** (`LogLevel`, `LogMessage`, `Formatter`); store raw facts, render later.
5. **Three orthogonal axes** — Singleton (access), Chain of Responsibility (level filter), Strategy (formatter + appender) — composed, not entangled.
6. **Sinks stay dumb** — they write bytes, never re-decide level or format.
7. **Meyers singleton + a mutex around dispatch** — lazy thread-safe init, no interleaving.
8. **Follow-ups = a queue stage or a new subclass** (async, rotation, hierarchy), never a rewrite of `log()`.

Follow that skeleton on any "shared service with pluggable behavior" LLD (rate limiter, metrics collector, notification dispatcher) and the pattern stack falls out almost mechanically.
