# LLD Practice Set (C++) — Index

A curated set of the most-asked Low-Level Design problems, each as a standalone problem statement in proper interview format: context, functional + non-functional requirements, expected C++ interface, the pattern being tested, and follow-ups.

## Easy — single subsystem, 1–2 patterns, minimal concurrency
1. [Tic-Tac-Toe](easy/01-tic-tac-toe.md) — Strategy (win rule), O(1) win-check
2. [Vending Machine](easy/02-vending-machine.md) — **State** pattern (canonical)
3. [Snake & Ladder](easy/03-snake-and-ladder.md) — entity modeling, pluggable dice
4. [ATM Machine](easy/04-atm-machine.md) — State + Chain of Responsibility (dispenser)
5. [Logging Framework](easy/05-logging-framework.md) — Singleton + Strategy + CoR

## Medium — interacting subsystems, real concurrency, several patterns
1. [Parking Lot](medium/01-parking-lot.md) — Strategy + Factory + pricing (most-asked)
2. [Elevator System](medium/02-elevator-system.md) — State + dispatch strategy (SCAN)
3. [Splitwise](medium/03-splitwise.md) — split Strategy + balance/debt graph
4. [LRU Cache](medium/04-lru-cache.md) — DS design + pluggable eviction + thread safety
5. [BookMyShow](medium/05-bookmyshow.md) — seat-lock concurrency + Observer + payment State

## Hard — rich domain, concurrency-heavy, extensibility under pressure
1. [Chess](hard/01-chess.md) — piece polymorphism + Command (undo/redo)
2. [Ride-Sharing](hard/02-ride-sharing.md) — state machines + matching + surge + Observer
3. [Food Delivery](hard/03-food-delivery.md) — multi-actor + order state machine + Observer
4. [Rate Limiter](hard/04-rate-limiter.md) — algorithm Strategy + thread safety + Factory
5. [Message Queue / Pub-Sub](hard/05-message-queue-pubsub.md) — Observer + producer-consumer + offsets

---

## How to use this set
- For each problem, spend the **first 2–3 minutes** writing assumptions + the public interface before any class internals. Interviewers score that habit heavily.
- Pattern overlaps to exploit: **State** (Vending ↔ ATM ↔ Elevator), **Chain of Responsibility** (ATM dispenser ↔ Logging levels), **Strategy** (almost everywhere), **Observer + concurrency** (BookMyShow ↔ Ride ↔ Pub-Sub).
- Suggested order: Vending Machine → Parking Lot → LRU Cache → BookMyShow → Chess.
