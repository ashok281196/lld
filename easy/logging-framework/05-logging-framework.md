# Logging Framework — LLD Problem Statement

**Difficulty:** Easy
**Language:** C++
**Pattern focus:** Singleton + Strategy (sinks) + Chain of Responsibility (levels)

---

## Context
Design a reusable logging library (think a lightweight log4j) for application developers to embed in their code.

## Functional Requirements
- Log levels with ordering: `DEBUG < INFO < WARN < ERROR`. Messages below the configured threshold are **dropped**.
- **Pluggable output sinks**: console, file, and (later) network — switchable without touching call sites.
- A **single global access point** for the logger, configurable once at startup (level, sink, format).
- **Thread-safe** writes: multiple threads logging concurrently must not interleave a single message.

## Non-Functional / Constraints
- The logger is a **Singleton** (one shared instance).
- Sinks are a **Strategy** (`Appender` interface) so output targets swap freely.
- Level filtering can be modeled as a **Chain of Responsibility** of level handlers.
- A `Formatter` abstraction controls the rendered message layout.

## Expected Public Interface
```cpp
enum class LogLevel { DEBUG, INFO, WARN, ERROR };

struct LogMessage { LogLevel level; std::string text; /* timestamp, thread id ... */ };

class Appender {                       // Strategy
public:
    virtual void append(const LogMessage&) = 0;   // ConsoleAppender, FileAppender
    virtual ~Appender() = default;
};

class Logger {
public:
    static Logger& getInstance();      // Singleton
    void log(LogLevel level, const std::string& msg);
    void setLevel(LogLevel level);
    void addAppender(std::unique_ptr<Appender>);
private:
    Logger() = default;                // non-copyable, non-movable
};
```

## What the Interviewer Is Really Testing
- Three patterns in one small surface: **Singleton**, **Strategy**, **Chain of Responsibility**.
- That you mention **thread safety** explicitly (mutex around the append, or an async queue).
- A `Formatter` abstraction earns bonus points.

## Follow-Up Questions to Expect
1. **Async logging**: hand messages to a background flush thread (a producer-consumer queue — squarely your home turf).
2. **Log rotation** by file size or date.
3. **Per-module loggers** with independent levels and a parent/child hierarchy.

## Your Task
1. Define the `Logger` singleton, `Appender` strategy, and `Formatter`.
2. Add the mutex for thread safety.
3. Attempt the async-queue follow-up.
