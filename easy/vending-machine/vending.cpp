#include <bits/stdc++.h>

using namespace std;

class VendingMachine;   // fundamental cycle: state <-> machine

// ---------- Data ----------
struct Product {
    string name;
    int price;
    int qty;
};

// ---------- State interface ----------
class VendingState{
  public:
    virtual void insertMoney(VendingMachine* m , int amount) = 0;
    virtual void selectProduct(VendingMachine* m, int code) = 0;
    virtual void dispenseProduct(VendingMachine* m) = 0;
    virtual void refund(VendingMachine* m) = 0;
    virtual ~VendingState() = default;
};

// ---------- Context ----------
class VendingMachine{
  private:
    VendingState* currentState;
    VendingState* idle;
    VendingState* hasMoney;
    VendingState* dispense;
    int balance = 0;
    int selectedCode = -1;
    map<int, Product> inventory;   // code -> product

  public:
    VendingMachine();    // body at bottom (needs concrete states)
    ~VendingMachine();

    void setState(VendingState* s){ currentState = s; }

    VendingState* getIdle(){ return idle; }
    VendingState* getHasMoney(){ return hasMoney; }
    VendingState* getDispense(){ return dispense; }

    // money
    void addBalance(int amt){ balance += amt; }
    int  getBalance(){ return balance; }
    void resetBalance(){ balance = 0; }

    // inventory
    void addProduct(int code, const Product& p){ inventory[code] = p; }
    bool hasStock(int code){ return inventory.count(code) && inventory[code].qty > 0; }
    int  getPrice(int code){ return inventory[code].price; }
    string getName(int code){ return inventory[code].name; }
    void reduceStock(int code){ inventory[code].qty--; }

    // selection
    void setSelected(int code){ selectedCode = code; }
    int  getSelected(){ return selectedCode; }

    // public API -> delegates to current state
    void insertMoney(int amount){ currentState->insertMoney(this, amount); }
    void selectProduct(int code){ currentState->selectProduct(this, code); }
    void dispenseProduct(){ currentState->dispenseProduct(this); }
    void refund(){ currentState->refund(this); }
};

// ---------- Concrete states (all inline now: no sibling class ever named) ----------
class IdleState : public VendingState{
  public:
    void insertMoney(VendingMachine* m, int amount) override {
        m->addBalance(amount);
        cout << "Money in: " << amount << " | balance=" << m->getBalance() << endl;
        m->setState(m->getHasMoney());
    }
    void selectProduct(VendingMachine* m, int code) override {
        cout << "Insert Money First" << endl;
    }
    void dispenseProduct(VendingMachine* m) override {
        cout << "Insert Money First" << endl;
    }
    void refund(VendingMachine* m) override {
        cout << "Nothing to refund" << endl;
    }
};

class HasMoneyState : public VendingState{
  public:
    void insertMoney(VendingMachine* m, int amount) override {
        m->addBalance(amount);
        cout << "Money in: " << amount << " | balance=" << m->getBalance() << endl;
    }
    void selectProduct(VendingMachine* m, int code) override {
        if(!m->hasStock(code)){
            cout << "Out of stock for code " << code << endl;
            return;
        }
        if(m->getBalance() < m->getPrice(code)){
            cout << "Insufficient money. Need " << m->getPrice(code)
                 << ", have " << m->getBalance() << endl;
            return;
        }
        m->setSelected(code);
        cout << "Selected: " << m->getName(code) << endl;
        m->setState(m->getDispense());
    }
    void dispenseProduct(VendingMachine* m) override {
        cout << "Select a product first" << endl;
    }
    void refund(VendingMachine* m) override {
        cout << "Refund: " << m->getBalance() << endl;
        m->resetBalance();
        m->setState(m->getIdle());
    }
};

class DispenseState : public VendingState{
  public:
    void insertMoney(VendingMachine* m, int amount) override {
        cout << "Dispensing in progress, cannot add money" << endl;
    }
    void selectProduct(VendingMachine* m, int code) override {
        cout << "Already dispensing" << endl;
    }
    void dispenseProduct(VendingMachine* m) override {
        int code = m->getSelected();
        int change = m->getBalance() - m->getPrice(code);
        m->reduceStock(code);
        cout << "Dispensed: " << m->getName(code) << endl;
        if(change > 0) cout << "Change returned: " << change << endl;
        m->resetBalance();
        m->setSelected(-1);
        m->setState(m->getIdle());
    }
    void refund(VendingMachine* m) override {   // changed mind before collecting
        cout << "Refund: " << m->getBalance() << endl;
        m->resetBalance();
        m->setSelected(-1);
        m->setState(m->getIdle());
    }
};

// ---------- ctor/dtor: only place that knows every concrete state ----------
VendingMachine::VendingMachine(){
    idle     = new IdleState();
    hasMoney = new HasMoneyState();
    dispense = new DispenseState();
    currentState = idle;
}
VendingMachine::~VendingMachine(){
    delete idle;
    delete hasMoney;
    delete dispense;
}

// ---------- demo ----------
int main(){
    VendingMachine m;
    m.addProduct(1, {"Coke", 25, 2});
    m.addProduct(2, {"Water", 15, 5});

    m.selectProduct(1);    // reject: insert money first
    m.insertMoney(10);     // idle -> hasMoney
    m.insertMoney(20);     // top up, balance=30
    m.selectProduct(1);    // ok -> dispense
    m.dispenseProduct();   // dispense Coke, change 5, -> idle

    return 0;
}
