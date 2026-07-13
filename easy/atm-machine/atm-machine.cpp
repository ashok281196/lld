#include <bits/stdc++.h>
using namespace std;

class MoneyHandler{
    protected:
        MoneyHandler* nextHandler;
    public: 
        MoneyHandler(){
            this->nextHandler = nullptr;
        }

        void setNextHandler(MoneyHandler* next){
            this->nextHandler = next;
        }

        virtual void dispense(int amount) = 0;
};


class ThousandHandler : public MoneyHandler{
    private:
        int numNotes;

    public:
        ThousandHandler(int numNotes){
            this->numNotes = numNotes;
        }

        void dispense(int amount){
            int notesNeeded= amount/1000;

            if(notesNeeded > numNotes){
                notesNeeded = numNotes;
                numNotes =0; 
            }else{
                numNotes-=notesNeeded;
            }

            if(notesNeeded>0){
                cout << "Despensing " << notesNeeded << " x 1000 Notes" << endl;
            }
            int remainingAmt =  amount - (notesNeeded*1000);

            if(remainingAmt>0){
                if(nextHandler!= nullptr){
                    nextHandler->dispense(remainingAmt);
                }else{
                    cout << "Remaining amount " << remainingAmt << " can't be fullfilled" << endl;
                }
            }
        }
};

class FiveHundresHandler : public MoneyHandler{
    private:
        int numNotes;

    public:
        FiveHundresHandler(int numNotes){
            this->numNotes = numNotes;
        }

        void dispense(int amount){
            int notesNeeded= amount/500;

            if(notesNeeded > numNotes){
                notesNeeded = numNotes;
                numNotes =0; 
            }else{
                numNotes-=notesNeeded;
            }

            if(notesNeeded>0){
                cout << "Despensing " << notesNeeded << " x 500 Notes" << endl;
            }
            int remainingAmt =  amount - (notesNeeded*500);

            if(remainingAmt>0){
                if(nextHandler!= nullptr){
                    nextHandler->dispense(remainingAmt);
                }else{
                    cout << "Remaining amount " << remainingAmt << " can't be fullfilled" << endl;
                }
            }
        }
};



class TwoHundresHandler : public MoneyHandler{
    private:
        int numNotes;

    public:
        TwoHundresHandler(int numNotes){
            this->numNotes = numNotes;
        }

        void dispense(int amount){
            int notesNeeded= amount/200;

            if(notesNeeded > numNotes){
                notesNeeded = numNotes;
                numNotes =0; 
            }else{
                numNotes-=notesNeeded;
            }

            if(notesNeeded>0){
                cout << "Despensing " << notesNeeded << " x 200 Notes" << endl;
            }
            int remainingAmt =  amount - (notesNeeded*200);

            if(remainingAmt>0){
                if(nextHandler!= nullptr){
                    nextHandler->dispense(remainingAmt);
                }else{
                    cout << "Remaining amount " << remainingAmt << " can't be fullfilled" << endl;
                }
            }
        }
};


class HundresHandler : public MoneyHandler{
    private:
        int numNotes;

    public:
        HundresHandler(int numNotes){
            this->numNotes = numNotes;
        }

        void dispense(int amount){
            int notesNeeded= amount/100;

            if(notesNeeded > numNotes){
                notesNeeded = numNotes;
                numNotes =0; 
            }else{
                numNotes-=notesNeeded;
            }

            if(notesNeeded>0){
                cout << "Despensing " << notesNeeded << " x 100 Notes" << endl;
            }
            int remainingAmt =  amount - (notesNeeded*100);

            if(remainingAmt>0){
                if(nextHandler!= nullptr){
                    nextHandler->dispense(remainingAmt);
                }else{
                    cout << "Remaining amount " << remainingAmt << " can't be fullfilled" << endl;
                }
            }
        }
};

int main(){

    MoneyHandler* thousandHandler = new ThousandHandler(3);
    MoneyHandler* fiveHundredHandler = new FiveHundresHandler(5);
    MoneyHandler* twoHundredHandler = new TwoHundresHandler(10);
    MoneyHandler* hundredHandler = new HundresHandler(20);

    thousandHandler->setNextHandler(fiveHundredHandler);
    fiveHundredHandler->setNextHandler(twoHundredHandler);
    twoHundredHandler->setNextHandler(hundredHandler);

    int amtToWithdraw =2800;

    cout << "Despensing Amount: " <<amtToWithdraw << endl;
    thousandHandler->dispense(amtToWithdraw);
    return 0;

}
