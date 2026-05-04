# Low-Level Design (LLD) Practice

A structured, hands-on repository for mastering Low-Level Design — built around incremental phases, classic patterns, and interview-style problems. Code is in **C++** with a thin CMake build.

> Goal: become fluent at translating fuzzy requirements into clean, extensible class designs in 30–45 minutes.

---


## Roadmap & Table of Contents

### [Phase 1 — Foundations (Week 1)](./phase-1-foundations/)

The mental model. Don't skip — every later phase rests on this.

- **OOP Pillars** — [folder](./phase-1-foundations/01-oop-pillars/)
  - [Encapsulation](./phase-1-foundations/01-oop-pillars/01-encapsulation/) — data hiding, why getters/setters matter
  - [Abstraction](./phase-1-foundations/01-oop-pillars/02-abstraction/) — interfaces vs abstract classes
  - [Inheritance](./phase-1-foundations/01-oop-pillars/03-inheritance/) — and why composition is often better
  - [Polymorphism](./phase-1-foundations/01-oop-pillars/04-polymorphism/) — runtime vs compile-time, dynamic dispatch
- **SOLID Principles** — [folder](./phase-1-foundations/02-solid/)
  - [S — Single Responsibility](./phase-1-foundations/02-solid/01-srp/)
  - [O — Open/Closed](./phase-1-foundations/02-solid/02-ocp/)
  - [L — Liskov Substitution](./phase-1-foundations/02-solid/03-lsp/)
  - [I — Interface Segregation](./phase-1-foundations/02-solid/04-isp/)
  - [D — Dependency Inversion](./phase-1-foundations/02-solid/05-dip/)
- **Other Key Concepts** — [folder](./phase-1-foundations/03-other-concepts/)
  - [Composition over Inheritance](./phase-1-foundations/03-other-concepts/01-composition-over-inheritance/)
  - [DRY, KISS, YAGNI](./phase-1-foundations/03-other-concepts/02-dry-kiss-yagni/)
  - [Coupling vs Cohesion](./phase-1-foundations/03-other-concepts/03-coupling-cohesion/)
  - [Law of Demeter](./phase-1-foundations/03-other-concepts/04-law-of-demeter/)

### Phase 2 — Design Patterns (Weeks 2–3) *(planned)*

Focus on high-frequency GoF patterns; the rest can wait.

- **Creational** — Singleton, Factory & Abstract Factory, Builder, Prototype
- **Structural** — Adapter, Decorator, Facade, Composite, Proxy
- **Behavioral** — Strategy, Observer, State, Command, Chain of Responsibility, Template Method, Iterator

### Phase 3 — UML & Modeling (Week 4) *(planned)*

- Class diagrams — association, aggregation, composition, inheritance, dependency
- Sequence diagrams — flow between objects
- State diagrams — state-driven systems

### Phase 4 — Classic LLD Problems (Weeks 4–7) *(planned)*

| Tier | Problems |
|------|----------|
| **1 — Warmup** | Parking Lot · Vending Machine · ATM · Library Management · Logger |
| **2 — Build-up** | Splitwise · Snake & Ladder / Chess / Tic-Tac-Toe · Elevator · BookMyShow · Online Cart · File System |
| **3 — Advanced** | Rate Limiter · LRU/LFU Cache · Notification System · Food Delivery · Cab Booking · Tinder · KV Store · Concurrent HashMap · Order Matching Engine |
| **4 — Hard** | Distributed Cache · Concurrent Job Scheduler · Type-ahead (Trie) · Calendar (recurring events) · Concurrent Inventory |

### Phase 5 — Concurrency in LLD (Week 5+, parallel with Phase 4) *(planned)*

- Thread-safe Singleton (double-checked locking, Meyers' singleton)
- Producer–Consumer (condition variables, blocking queue)
- Read-Write locks (`std::shared_mutex`)
- Thread pools (`std::async`, custom executor)
- Synchronization primitives — mutex, semaphore, atomics
- Concurrent variants of LRU, Rate Limiter, Logger

---

## How to Approach an LLD Interview

A clean structure interviewers love (≈ 60 min):

1. **Clarify requirements (5 min)** — users, must-have vs nice-to-have, scale, concurrency, persistence.
2. **Identify entities & actors (5 min)** — list the nouns; they usually become classes.
3. **Define relationships (5 min)** — quick class diagram. Lean on composition.
4. **Identify behaviors & patterns (10 min)** — where does state change? where are interchangeable algorithms? where will requirements churn? Apply patterns *because they fit*, not to show off.
5. **Code core classes (20–25 min)** — start with interfaces and key classes. Show enums, exceptions, thread safety where relevant.
6. **Walk through a sample flow (5 min)** — *"User parks a car"* → trace through the objects.
7. **Discuss extensions (5 min)** — EV charging? multiple floors? prove the design is open for extension.

---