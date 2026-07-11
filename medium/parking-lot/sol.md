# Parking Lot — LLD Interview Guide (C++, beginner-friendly)

A one-hour walkthrough with code you can read top to bottom. The point of an LLD round is
**not** clever syntax — it's showing you clarify scope, pick clean objects, apply good OOP
(**encapsulation!**), and explain trade-offs. This version keeps the code simple *and*
properly encapsulated: **data is private, behaviour is public.** A short **"Level it up"**
section at the end shows what a senior would add — say those things out loud even if you
don't code them.

---

## 0. Time budget (1 hour)

| Phase | Time | What you're doing |
| --- | --- | --- |
| Clarify requirements | 0–8 min | Ask questions, lock scope out loud |
| Entities + relationships | 8–15 min | Sketch classes, name the pieces |
| Code core classes | 15–45 min | Enums → Vehicle → Spot → Floor → Ticket → Lot |
| Demo / `main` walkthrough | 45–55 min | Park, unpark, prove it works |
| Extensions + Q&A | 55–60 min | How you'd harden and grow it |

Rule of thumb: **don't write a class until you've said your assumptions aloud and the
interviewer has agreed.**

---

## 1. Clarifying questions to ask the interviewer

Ask these first, grouped so you sound structured. For each, the **default** is what to
assume if they say "you decide."

**Scope & structure**

- One lot with multiple floors, or many lots? → *One lot, multiple floors.*
- Multiple entrances issuing tickets at once? → *Assume yes (note it as a concurrency concern).*
- Roughly how many spots? → *A few thousand, fits in memory.*

**Vehicles & spots**

- Which vehicle types? → *Motorcycle, Car, Truck.*
- Which spot sizes, and the fit rule? → *Small/Medium/Large; bike fits any, car needs ≥ Medium, truck needs Large.*
- Can one vehicle take multiple spots? → *No — one vehicle, one spot.*
- Special spots (EV, handicapped)? → *Out of scope v1; mention as an extension.*

**Pricing & payment**

- Flat, hourly, or per vehicle type? → *Hourly, per type, minimum 1 hour, rounded up.*
- Payment methods / lost ticket? → *Out of scope v1.*

**Assignment policy**

- Nearest spot, any free spot, load-balanced? → *Nearest-first (first free compatible bay).*

---

## 2. Locked scope (say this back before coding)

> "So v1: one lot, multiple floors. Three vehicle types, three spot sizes with a fit
rule. Hourly pricing by vehicle type. Nearest-free-spot assignment. Ticket in, fee out.
I'll keep pricing and spot-finding simple for now and mention how I'd make them swappable
later. Sound good?"
> 

That agreement is the contract for the rest of the hour.

---

## 3. Entities & relationships

```
ParkingLot            the front door; the only thing the outside world calls
 ├── has many  ParkingFloor        one level
 │                └── has many ParkingSpot   one bay (owns its size + who's in it)
 │                                   └── points to a Vehicle while occupied
 ├── keeps a list of Ticket        one per parked vehicle
 └── calculateFee()  is a private helper (pricing is an internal detail)

Vehicle  ── is the base of ──▶  Motorcycle | Car | Truck
```

Design choices worth saying out loud:

- **Encapsulation: data private, behaviour public.** No outside code flips a bay's
`occupied` flag directly — it calls `park()` / `leave()`. Each class guards its own state.
- **Put each rule on the object that owns the data it needs.** `canFit` lives on
`ParkingSpot` because it depends on the bay's size. Pricing lives inside `ParkingLot`
as a private helper.
- **The spot points at the vehicle, it doesn't own it.** The driver creates the car (in
`main`); the bay just holds a non-owning pointer that's `nullptr` when empty.
- **`ParkingLot` is the front door.** Everything goes through its public methods; nobody
reaches into floors or spots from outside.

---

## 4. The implementation (single file)

Build it in this order on the board. The file below is the whole thing.

```cpp
// ============================================================================
//  PARKING LOT  —  Low-Level Design (beginner-friendly, properly encapsulated)
//  C++.  Compile:  g++ -std=c++17 parking_lot_simple.cpp -o parking_lot
//  Rule of thumb used here: data is PRIVATE, behaviour is PUBLIC.
//  Each class guards its own state; the outside world talks through methods.
// ============================================================================
#include <iostream>
#include <string>
#include <vector>
#include <cmath>      // for ceil()

using namespace std;

// ------------------------------------------------------------- Enums --------
enum class VehicleType { MOTORCYCLE, CAR, TRUCK };
enum class SpotType    { SMALL, MEDIUM, LARGE };

string toString(VehicleType type) {
    if (type == VehicleType::MOTORCYCLE) return "Motorcycle";
    if (type == VehicleType::CAR)        return "Car";
    if (type == VehicleType::TRUCK)      return "Truck";
    return "Unknown";
}

// ------------------------------------------------------------ Vehicle -------
// Data is private; you can only read it through getters.
class Vehicle {
private:
    string      licensePlate_;
    VehicleType type_;
public:
    Vehicle(string plate, VehicleType type) {
        licensePlate_ = plate;
        type_ = type;
    }
    string      getLicensePlate() const { return licensePlate_; }
    VehicleType getType()         const { return type_; }
};

class Motorcycle : public Vehicle {
public:
    Motorcycle(string plate) : Vehicle(plate, VehicleType::MOTORCYCLE) {}
};
class Car : public Vehicle {
public:
    Car(string plate) : Vehicle(plate, VehicleType::CAR) {}
};
class Truck : public Vehicle {
public:
    Truck(string plate) : Vehicle(plate, VehicleType::TRUCK) {}
};

// ------------------------------------------------------- ParkingSpot --------
// A bay owns its state. Nobody can flip 'occupied' directly — they must call
// park()/leave(). The fit rule lives here because it depends on the bay's size.
class ParkingSpot {
private:
    string   id_;
    SpotType type_;
    bool     occupied_;
    Vehicle* parkedVehicle_;   // non-owning: points at the car while parked

public:
    ParkingSpot(string id, SpotType type) {
        id_ = id;
        type_ = type;
        occupied_ = false;
        parkedVehicle_ = nullptr;
    }

    string getId() const { return id_; }
    bool   isFree() const { return !occupied_; }

    // Can this vehicle fit in this bay? Bike fits any, car >= Medium, truck Large.
    bool canFit(VehicleType vt) const {
        if (vt == VehicleType::MOTORCYCLE) return true;
        if (vt == VehicleType::CAR)   return type_ == SpotType::MEDIUM || type_ == SpotType::LARGE;
        if (vt == VehicleType::TRUCK) return type_ == SpotType::LARGE;
        return false;
    }

    void park(Vehicle* vehicle) {
        parkedVehicle_ = vehicle;
        occupied_ = true;
    }
    void leave() {
        parkedVehicle_ = nullptr;
        occupied_ = false;
    }
};

// ------------------------------------------------------- ParkingFloor -------
// A floor owns its bays. It hands back a bay through methods, never the list.
class ParkingFloor {
private:
    int                 number_;
    vector<ParkingSpot> spots_;

public:
    ParkingFloor(int number) { number_ = number; }

    int getNumber() const { return number_; }

    void addSpot(string id, SpotType type) {
        spots_.push_back(ParkingSpot(id, type));
    }

    // First free bay that fits the vehicle, or nullptr if none on this floor.
    ParkingSpot* findFreeSpot(VehicleType vt) {
        for (ParkingSpot& spot : spots_) {
            if (spot.isFree() && spot.canFit(vt)) return &spot;
        }
        return nullptr;
    }

    // Look a bay up by id (used when a car leaves).
    ParkingSpot* findSpotById(string id) {
        for (ParkingSpot& spot : spots_) {
            if (spot.getId() == id) return &spot;
        }
        return nullptr;
    }

    int countFree() {
        int count = 0;
        for (ParkingSpot& spot : spots_) {
            if (spot.isFree()) count++;
        }
        return count;
    }
};

// ------------------------------------------------------------- Ticket -------
class Ticket {
private:
    string      id_;
    string      spotId_;
    string      plate_;
    VehicleType vehicleType_;

public:
    Ticket(string id, string spotId, string plate, VehicleType type) {
        id_ = id;
        spotId_ = spotId;
        plate_ = plate;
        vehicleType_ = type;
    }
    string      getId()          const { return id_; }
    string      getSpotId()      const { return spotId_; }
    VehicleType getVehicleType() const { return vehicleType_; }
};

// ----------------------------------------------------------- ParkingLot -----
// The front door. All state is private; the world uses the public methods only.
class ParkingLot {
private:
    vector<ParkingFloor> floors_;
    vector<Ticket>       tickets_;
    int                  ticketCounter_ = 0;

    // Pricing is an internal detail, so it's a private helper.
    double calculateFee(VehicleType type, double hours) {
        double billableHours = ceil(hours);        // round up: 2.5h -> 3h
        if (billableHours < 1) billableHours = 1;  // minimum one hour

        double rate = 0;
        if (type == VehicleType::MOTORCYCLE) rate = 10;
        else if (type == VehicleType::CAR)   rate = 20;
        else if (type == VehicleType::TRUCK) rate = 40;

        return rate * billableHours;
    }

public:
    // ---- setup ----
    void addFloor(int number) {
        floors_.push_back(ParkingFloor(number));
    }
    void addSpot(string id, SpotType type) {
        floors_.back().addSpot(id, type);   // add to the most recent floor
    }

    // ---- park: ask each floor (in order) for a free spot ----
    void parkVehicle(Vehicle* vehicle) {
        for (ParkingFloor& floor : floors_) {
            ParkingSpot* spot = floor.findFreeSpot(vehicle->getType());
            if (spot != nullptr) {
                spot->park(vehicle);

                ticketCounter_++;
                string ticketId = "T" + to_string(ticketCounter_);
                tickets_.push_back(Ticket(ticketId, spot->getId(),
                                          vehicle->getLicensePlate(), vehicle->getType()));

                cout << "[PARK]  " << toString(vehicle->getType()) << " "
                     << vehicle->getLicensePlate() << " -> spot " << spot->getId()
                     << "  (ticket " << ticketId << ")\n";
                return;
            }
        }
        cout << "[FULL]  no spot for " << toString(vehicle->getType())
             << " " << vehicle->getLicensePlate() << "\n";
    }

    // ---- unpark: free the bay and charge the driver ----
    double unparkVehicle(string ticketId, double hours) {
        for (int i = 0; i < (int)tickets_.size(); i++) {
            if (tickets_[i].getId() == ticketId) {
                Ticket ticket = tickets_[i];

                for (ParkingFloor& floor : floors_) {
                    ParkingSpot* spot = floor.findSpotById(ticket.getSpotId());
                    if (spot != nullptr) spot->leave();
                }

                double fee = calculateFee(ticket.getVehicleType(), hours);
                cout << "[EXIT]  ticket " << ticketId << " from spot "
                     << ticket.getSpotId() << "  | " << hours << "h -> fee $" << fee << "\n";

                tickets_.erase(tickets_.begin() + i);
                return fee;
            }
        }
        cout << "[ERROR] invalid ticket " << ticketId << "\n";
        return -1;
    }

    void printAvailability() {
        cout << "--- availability ---\n";
        for (ParkingFloor& floor : floors_) {
            cout << "  floor " << floor.getNumber() << ": "
                 << floor.countFree() << " free\n";
        }
    }
};

// ---------------------------------------------------------------- demo ------
int main() {
    ParkingLot lot;

    lot.addFloor(1);
    lot.addSpot("F1-S1", SpotType::SMALL);
    lot.addSpot("F1-M1", SpotType::MEDIUM);
    lot.addSpot("F1-L1", SpotType::LARGE);

    lot.addFloor(2);
    lot.addSpot("F2-M1", SpotType::MEDIUM);
    lot.addSpot("F2-L1", SpotType::LARGE);

    lot.printAvailability();

    Motorcycle bike("KA01-1111");
    Car        car("KA02-2222");
    Truck      truck("KA03-3333");

    lot.parkVehicle(&bike);
    lot.parkVehicle(&car);
    lot.parkVehicle(&truck);
    lot.printAvailability();

    lot.unparkVehicle("T2", 2.5);   // car: ceil(2.5)=3, rate 20 -> $60
    lot.printAvailability();

    Car car2("KA04-4444");          // reuses F1-M1 that the first car freed
    lot.parkVehicle(&car2);
    lot.printAvailability();

    return 0;
}
```

### Verified output

```
--- availability ---
  floor 1: 3 free
  floor 2: 2 free
[PARK]  Motorcycle KA01-1111 -> spot F1-S1  (ticket T1)
[PARK]  Car KA02-2222 -> spot F1-M1  (ticket T2)
[PARK]  Truck KA03-3333 -> spot F1-L1  (ticket T3)
--- availability ---
  floor 1: 0 free
  floor 2: 2 free
[EXIT]  ticket T2 from spot F1-M1  | 2.5h -> fee $60
--- availability ---
  floor 1: 1 free
  floor 2: 2 free
[PARK]  Car KA04-4444 -> spot F1-M1  (ticket T4)
--- availability ---
  floor 1: 0 free
  floor 2: 2 free
```

---

## 5. Walk the interviewer through it

Say these lines as you point at the code — this is what earns marks:

- **Encapsulation is deliberate.** Every field is `private`; the outside world only ever
calls methods. A bay can't be marked occupied except by `park()`. State each class
protects its own invariants.
- **Enums** name the choices so we never pass raw strings around.
- **Vehicle + subclasses** show the hierarchy. Each subclass just sets its type.
- **`ParkingSpot::canFit`** is the fit rule, living on the object that has the size it needs.
- **`ParkingFloor::findFreeSpot`** hides the inner loop — the lot asks the floor, it doesn't
reach into the floor's list of spots.
- **`ParkingLot::parkVehicle`** is nearest-first: ask each floor in order for a free spot,
take the first one, issue a ticket, stop. Nearest-first *is* the loop order.
- **`ParkingLot::calculateFee`** is a private helper — pricing is an internal detail, not
something callers should see or depend on.
- **`ParkingLot` is the front door** — `main` only calls `addFloor`, `addSpot`,
`parkVehicle`, `unparkVehicle`, `printAvailability`.

---

## 6. Level it up (say these out loud — this is the senior signal)

The code above is intentionally simple. Here's how you'd grow it, and *why*:

- **Make pricing swappable (Strategy pattern).** `calculateFee` is a private helper today.
For day/night rates, weekend surge, or a free first 15 minutes, promote it to a
`FeeStrategy` interface with subclasses and hand one to the lot. New rule = new class.
Same idea for spot-selection. Say: *"pricing and placement are things that change, so I'd
put them behind interfaces."*
- **Fix ownership with smart pointers.** In a larger program the lot would own spots and
tickets via `unique_ptr` so nothing leaks. Here I keep it simple with values and one
non-owning pointer.
- **Handle two entrances at once (concurrency).** If two cars race for the last spot both
could grab it. Guard `parkVehicle` / `unparkVehicle` with a `mutex`, or use an atomic
free-count per floor. Mention the race even if you don't code it.
- **Speed up exit and availability.** Finding the spot on exit is a loop today. A
`map<string, ParkingSpot*>` from id → spot makes it O(1); a cached free-count makes
`countFree` O(1).
- **Special spots / payment / persistence.** EV and handicapped bays = new `SpotType`
values plus a rule; payment becomes its own class; storage becomes a database layer, all
behind the same `ParkingLot` front door.

---

## 7. Likely follow-up questions + short answers

- *Why is the vehicle held as a plain pointer?* — It just points at the parked car; the
driver owns the car, not the lot. `nullptr` means the bay is empty.
- *Why did you put `canFit` on the spot and `calculateFee` on the lot?* — Each behaviour
sits with the data it needs. The fit rule needs the bay's size; pricing needs nothing but
the type and hours and is a lot-level policy.
- *How is "nearest" decided?* — Loop order: lower floors first, earlier spots first. To
change the policy I'd swap in a different spot-finder (the Strategy idea).
- *Where would this break under load?* — Two entrances parking at the same instant. Fix
with a lock or an atomic free-count.
- *How would you test it?* — `canFit` for every vehicle/spot pair, `calculateFee` for 0h,
2.5h and each type, then park-until-full, unpark-and-reuse, and a bad ticket.
- *How do you make exit O(1)?* — Keep a `map<string, ParkingSpot*>` from spot id to spot.

---

## 8. If you're running out of time

Code in this priority order and say so: **enums → Vehicle → ParkingSpot (+ `canFit`) →
ParkingLot with `parkVehicle`/`unparkVehicle`**. That alone is a working system. Floors,
tickets, and the fee are next. The "level it up" items you can just *describe* — stating the
upgrade earns most of the credit.