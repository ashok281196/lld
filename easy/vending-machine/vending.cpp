#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
using namespace std;

enum class Coin {
    ONE = 1,
    FIVE = 5,
    TEN = 10,
    TWENTY = 20
};

inline int valueof(Coin c) {
    return static_cast<int>(c);
}

struct Product {
    string name;
    string code;
    int price = 0;
};

class Inventory {
    public:
        void add(const Product& p, int qty){
            auto it = entries_.find(p.code);
            if(it ==entiries_.end()){
                entries_[p.code] = {p, qty};
            }else{
                it->second.qty += qty;
            }
        }

        bool hasStock(const string& code) const {
            auto it = entries_.find(code);
            return it != entries_.end() && it->second.qty > 0;
        }

        const Product& product(const string& code) const {
            return entries_.at(code).product;
        }

        void reduceOne(const string& code){
            entries_.at(code).qty--;

        }

    private:
        struct Entry {
            Product product;
            int qty = 0;
        };

        unordered_map<string, Entry> entries_;

}

class VendingMachine;

class State {
    public:
        virtual ~State() = default;
        virtual void selectProduct(VendingMachine&, const string&) = 0;
        virtual void insertCoin(VendingMachine&, Coin) = 0;
        virtual Product dispense(VendingMachine&) = 0;
        virtual void cancel(VendingMachine&) = 0;
        virtual string name() const = 0;
};

class VendingMachine{
    public: 
       VendingMachine();

       void selectProduct(const string& code){
        state_->selectProduct(*this, code);
       }

       void insertCoin(Coin c){
        state_->insertCoin(*this, c);
       }

       Product dispense(){
            return state_->dispense(*this);
      }

      void cancel(){
        state_->cancel(*this);
      }

      void refill(const Product& p, int qty){
        inventory_.add(p, qty);
      }

      int collectCash(int c = cashBox_, cashBox_ = 0){
        return c;
      }

      void setState(State* s){
        state_ = s;
      }

      State* idle(){
        return idle_.get();
      }

      State* hasMoney(){
        return hasMoney_.get();
      }

      State* dispensing(){
        return dispensing_.get();
      }

      Inventory& inventory(){
        return inventory_;
      }

      int balance() const {
        return balance_;
      }

      void addBalance(int amount){
        balance_ += amount;
      }

      int refundBalance(){
        int amount = balance_;
        balance_ = 0;
        return amount;
      }

      void bankBalance(){
        cashBox += balance_;
        balance_ = 0;
      }

      const string& selected() const {
        return selectedCode_;
      }

      void setSelected(const string& code){
        selectedCode_ = code;
      }

    private:
        unique_ptr<State> idle_, hasMoney_, dispensing_;
        State* state_ = nullptr;

        Inventory inventory_;
        int balance_ = 0;
        int cashBox = 0;
        string selectedCode_;

};

static string makeChange(int amount){
    const int coins[] = {20, 10, 5, 1};
    string out; 
    for(int c : coins){
        while(amount >= c){
            amount -= c;
            out += to_string(c) + " ";
        }
    }
    return out;
}

class IdleState : public State{
    public: 
        void selectproduct(VendingMachine& m, const string& code) override{
            if(!m.inventory().hasStock(code)){
                throw invalid_argument("Out of Stock" + code);
            } else {
                m.setSelected(code);
                m.setState(m.hasMoney());
            }
        }

        
};