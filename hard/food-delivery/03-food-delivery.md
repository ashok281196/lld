# Food Delivery (Swiggy / Zomato) — LLD Problem Statement

**Difficulty:** Hard
**Language:** C++
**Pattern focus:** Multi-actor coordination + order lifecycle State machine + Strategy (matching) + Observer

---

## Context
Design the order-and-delivery core of a food-delivery platform connecting customers, restaurants, and delivery partners.

## Functional Requirements
- **Customers** browse **restaurants** (by location / cuisine), view **menus**, build a **cart**, and place an **order**.
- **Restaurants** accept/reject orders and mark food **ready**.
- **Delivery partners** are assigned, pick up, and deliver.
- **Order lifecycle**: `PLACED → ACCEPTED → PREPARING → READY → PICKED_UP → DELIVERED` (plus `REJECTED / CANCELLED`).
- Assign a suitable **delivery partner** near the restaurant when the order is accepted.
- Notify all actors on relevant state changes; compute the **bill** (items + taxes + delivery fee).

## Non-Functional / Constraints
- **Order modeled as a State machine**; invalid transitions rejected.
- **Delivery-partner assignment** is a pluggable Strategy (nearest, batching multiple orders).
- **Observer** for customer/restaurant/partner notifications.
- Three independently-acting actors → keep their state consistent; assignment must be atomic.

## Expected Public Interface
```cpp
enum class OrderStatus { PLACED, ACCEPTED, PREPARING, READY, PICKED_UP, DELIVERED, REJECTED, CANCELLED };

class PartnerAssignmentStrategy {     // Strategy
public:
    virtual DeliveryPartner* assign(const Order&, const std::vector<DeliveryPartner>&) = 0;
    virtual ~PartnerAssignmentStrategy() = default;
};

class OrderService {
public:
    Order placeOrder(const std::string& customerId, const std::string& restaurantId, const Cart& cart);
    void  restaurantRespond(const std::string& orderId, bool accept);
    void  markReady(const std::string& orderId);          // triggers/locks-in partner assignment
    void  markPickedUp(const std::string& orderId);
    void  markDelivered(const std::string& orderId);
};

class SearchService {
public:
    std::vector<Restaurant> search(Location loc, const std::string& cuisine);
};
```

## What the Interviewer Is Really Testing
- A correct **order State machine** with guarded transitions across **three actors**.
- **Atomic partner assignment** (no partner double-booked).
- Pluggable **assignment strategy** and clean **Observer** notifications.
- Whether `Cart`, `MenuItem`, `Restaurant`, `Order` are modeled cleanly with billing separated out.

## Follow-Up Questions to Expect
1. **Order batching**: one partner carries multiple nearby orders.
2. **ETA estimation** (prep time + travel time).
3. **Coupons / offers** and a pluggable pricing/discount engine.
4. **Live tracking** and reassignment if a partner cancels mid-delivery.

## Your Task
1. Assumptions + interface, then the actor and order entities.
2. Implement the order State machine with guarded transitions.
3. Add an assignment strategy + Observer; attempt batching as the headline follow-up.
