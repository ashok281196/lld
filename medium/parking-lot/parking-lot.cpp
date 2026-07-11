#include <bits/stdc++.h>

using namespace std;

enum class VehicleType{
    MOTORCYCLE,
    CAR,
    TRUCK
};

enum class SpotType{
    SMALL,
    MEDIUM,
    LARGE
};

string toString(VehicleType type) {
    if (type == VehicleType::MOTORCYCLE) return "Motorcycle";
    if (type == VehicleType::CAR)        return "Car";
    if (type == VehicleType::TRUCK)      return "Truck";
    return "Unknown";
}



class Vehicle{
    private:
        string licencePlate_;
        VehicleType type_;

    public:
        Vehicle(string plate, VehicleType type){
            licencePlate_ = plate;
            type_ = type;
        }

        string getLicencePlate() const {
            return licencePlate_;
        }

        VehicleType getVehicleType() const {
            return type_;
        }
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


class ParkingSpot{
    private:
        string id_;
        SpotType type_;
        bool occupied_;
        Vehicle* parkedVehicle_;

    public:
        ParkingSpot(string id, SpotType type){
            id_ = id;
            type_ = type;
            occupied_ = false;
            parkedVehicle_ = nullptr;
        }

        string getId() const { return id_; }

        bool isFree() const { return !occupied_; }

        bool canFit(VehicleType vehicle) const {
            if(vehicle == VehicleType::MOTORCYCLE){ return true; }
            if(vehicle == VehicleType::CAR){
                return type_ == SpotType::MEDIUM || type_ == SpotType::LARGE;
            }
            if(vehicle == VehicleType::TRUCK){
                return type_ == SpotType::LARGE;
            }
            return false;
        }

        void park(Vehicle* vehicle){
            parkedVehicle_ = vehicle;
            occupied_ = true;
        }

        void leave(){
            parkedVehicle_ = nullptr;
            occupied_ = false;
        }
};

class ParkingFloor{
    private:
       int number_;
       vector<ParkingSpot> spots_;

    public:
        ParkingFloor(int number){
            number_ = number;
        }

        int getNumber(){
            return number_;
        }

        void addSpot(string id, SpotType type){
            spots_.push_back(ParkingSpot(id, type));
        }

        ParkingSpot* findFreeSpot(VehicleType vt){
            for(ParkingSpot& spot : spots_){
                if(spot.isFree() && spot.canFit(vt)){
                    return &spot;
                }
            }
            return nullptr;
        }

        ParkingSpot* findSpotById(string id){
            for(ParkingSpot& spot : spots_){
                if(spot.getId() == id){
                    return &spot;
                }
            }
            return nullptr;
        }

        int countFree(){
            int count = 0;
            for(ParkingSpot& spot : spots_){
                if(spot.isFree()){
                    count++;
                }
            }
            return count;
        }
};


class Ticket{
    private:
        string id_;
        string spotId_;
        string plate_;
        VehicleType vehicleType_;

    public:
        Ticket(string id, string spotId, string plate, VehicleType type){
            id_ = id;
            spotId_ = spotId;
            plate_ = plate;
            vehicleType_ = type;
        }

        string getId(){
            return id_;
        }
        string getSpotId(){
            return spotId_;
        }
        VehicleType getVehicleType(){
            return vehicleType_;
        }
};

class ParkingLot{
    private:
        vector<ParkingFloor> floors_;
        vector<Ticket> tickets_;
        int ticketCounter_ = 0;

        double calculateFee(VehicleType type, double hours){
            double billableHours = ceil(hours);
            if(billableHours < 1) billableHours = 1;
            double rate = 0;
            if(type == VehicleType::MOTORCYCLE) rate = 10;
            if(type == VehicleType::CAR) rate = 20;
            if(type == VehicleType::TRUCK) rate = 40;

            return rate * billableHours;
        }

    public:
        void addFloor(int number){
            floors_.push_back(ParkingFloor(number));
        }
        void addSpot(string id, SpotType type){
            floors_.back().addSpot(id, type);
        }

        void parkVehicle(Vehicle* vehicle){
            for(ParkingFloor& floor : floors_){
                ParkingSpot* spot = floor.findFreeSpot(vehicle->getVehicleType());
                if(spot != nullptr){
                    spot->park(vehicle);
                    ticketCounter_++;
                    string ticketId = "T" + to_string(ticketCounter_);
                    tickets_.push_back(Ticket(ticketId, spot->getId(), vehicle->getLicencePlate(), vehicle->getVehicleType()));
                    cout << "[Park] " << toString(vehicle->getVehicleType()) << " " << vehicle->getLicencePlate()
                    << " -> spot " << spot->getId() << "  (ticket " << ticketId << ")" << endl;
                    return;
                }
            }
            cout << "[FULL]  no spot for " << toString(vehicle->getVehicleType())
             << " " << vehicle->getLicencePlate() << "\n";
        }

        double unparkVehicle(string ticketId, double hours){
            for(int i = 0; i < (int)tickets_.size(); i++){
                if(tickets_[i].getId() == ticketId){
                    Ticket ticket = tickets_[i];

                    for(ParkingFloor& floor : floors_){
                        ParkingSpot* spot = floor.findSpotById(ticket.getSpotId());
                        if(spot != nullptr) spot->leave();
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


int main(){
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
