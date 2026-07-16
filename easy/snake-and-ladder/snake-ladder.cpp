#include <bits/stdc++.h>

using namespace std;

class IObserver{
    public:
        virtual void update(string msg) = 0;
        virtual ~IObserver(){}
};

//Sample Observer Implemetation

class SnakeAndLadderConsoleNotifier : public IObserver{
    public: 
        void update(string msg){
            cout << "[Notification] " << msg <<endl;
        }
};

class Dice{
    private:
        int faces;

    public:
        Dice(int f){
            faces = f;
            srand(time(0));
        }

        int roll(){
            return (rand()%(this->faces)) +1;
        }
};

class BoardEntity{
    private:
        int startPostion;
        int endPostion;

    public:
        BoardEntity(int start, int end){
            startPostion = start;
            endPostion = end;
        }

        int getStart(){
            return startPostion;
        }

        int getEnd(){
            return endPostion;
        }
        
        virtual void display() = 0;
        virtual string name() = 0 ;
        virtual ~BoardEntity(){};
};

class Snake : public BoardEntity{
    public:
        Snake(int start, int end):BoardEntity(start, end){
            if(start>=end){
                cout<< "Invalid Snake" << endl;
             }
        }

        void display() override{
            cout <<  "Snake: " << getStart() <<  " -> " << getEnd() << endl;
        }

        string name() override {
            return "SNAKE";
        }

};

class Ladder : public BoardEntity{
    public:
        Ladder(int start, int end):BoardEntity(start, end){
            if(start<=end){
                cout<< "Invalid Ladder" << endl;
             }
        }

        void display() override{
            cout <<  "Ladder: " << getStart() <<  " -> " << getEnd() << endl;
        }

        string name() override {
            return "LADDER";
        }

};

class BoardSetupStrategy;

class Board{
    private:
        int size;
        vector<BoardEntity*> snakeAndLadder;
        map<int, BoardEntity*> boardEntities;

    public:
        Board(int s){
            size = s;
        }

        bool canAddEntities(int pos){
            return boardEntities.find(pos) == boardEntities.end();
        }

        void addBoardEntities(BoardEntity* boardEntity){    
                if(canAddEntities(boardEntity->getStart())){
                    snakeAndLadder.push_back(boardEntity);
                    boardEntities[boardEntity->getStart()] = boardEntity;
                }

        }

        void setupBoard(BoardSetupStrategy* stategy);

        BoardEntity* getEntity(int pos){
            if(boardEntities.find(pos) != boardEntities.end()){
                return boardEntities[pos];
            }

            return nullptr;
        }

        int getBoardSize(){
            return size;
        }

        void display(){
            cout<< "\n=== Board Configurations === \n" << endl;
            cout << "Board Size: " << size <<" Cells" << endl;

            int snakeCount =0 ; 
            int ladderCount =0 ;
            for(auto entity : snakeAndLadder){
                if(entity->name()=="SNAKE"){
                    snakeCount++;
                }else{
                    ladderCount++;
                }
            }

            cout << "\n === Snakes:" <<snakeCount <<endl;
            for(auto entity: snakeAndLadder){
                if(entity->name()=="Snake"){
                    entity->display();
                }
            }

            cout << "\n === Ladders:" <<ladderCount <<endl;
            for(auto entity: snakeAndLadder){
                if(entity->name()=="LADDER"){
                    entity->display();
                }
            }

            cout << "==============================================" <<endl;

        }

        ~Board(){
            for(auto entity:snakeAndLadder){
                delete(entity);
            }
        }
};

class BoardSetupStrategy{
    public:
        virtual void setupBoard(Board* baord) =0;
        virtual ~BoardSetupStrategy(){}
};

class RandomBoardStrategy : public BoardSetupStrategy{
    public:
        enum Difficulty{
            EASY,
            MEDIUM,
            HARD
        };
    private:
        Difficulty difficulty;

        void setupWithProbability(Board* board, double snakeProbability){
            int boardSize = board->getBoardSize();
            int totalEntities = boardSize/10;
            
            for(int i =0; i<totalEntities; i++){
                double prob =(double)rand()/RAND_MAX;
                if(prob<snakeProbability){
                    int attempts = 0; 
                    while(attempts<50){
                        int start  = rand()%(boardSize-10) + 10;
                        int end = rand()%(start-1)+1;
                        if(board->canAddEntities(start)){
                            board->addBoardEntities(new Snake(start, end));
                            break;
                        }
                        attempts++;
                     }
                 }else{
                    int attempts = 0; 
                    while(attempts<50){
                        int start  = rand()%(boardSize-10) + 1;
                        int end = rand()%(boardSize-start) + start + 1;
                        if(board->canAddEntities(start) && end<boardSize){
                            board->addBoardEntities(new Ladder(start, end));
                            break;
                        }
                        attempts++;

                    }
            }
        }
    }

    public:
        RandomBoardStrategy(Difficulty d){
            difficulty =d;
        }

        void setupBoard(Board* board) override{
            switch(difficulty){
                case EASY:
                    setupWithProbability(board, 0.3);
                    break;
                case MEDIUM:
                    setupWithProbability(board ,0.5);
                    break;
                case HARD:
                    setupWithProbability(board, 0.7);
                    break;
            }
        }
    
};

class CustomCountStrategy : public BoardSetupStrategy{
    private:
        int numLadders;
        int numSnakes;
        bool randomPostions;
        vector<pair<int, int>> snakePostions;
        vector<pair<int, int>> ladderPostions;
    
    public:
        CustomCountStrategy(int snakes, int ladders, bool random){
            numSnakes = snakes;
            numLadders = ladders;
            randomPostions = random;
        }

        void addSnakePosition(int start, int end){
            snakePostions.push_back(make_pair(start, end));
        }
        void addLadderPosition(int start, int end){
            ladderPostions.push_back(make_pair(start, end));
        }

        void setupBoard(Board* board)override{
            if(randomPostions){
                int boardSize = board->getBoardSize();
                int snakesAdded= 0 ;
                while(snakesAdded<numSnakes){
                    int start = rand()%(boardSize-10)+10;
                    int end = rand()%(start-1)+1;
                    if(board->canAddEntities(start)){
                        board->addBoardEntities(new Snake(start, end));
                        snakesAdded++;
                    }
                }

                int laddersAdded= 0 ;
                while(laddersAdded<numLadders){
                    int start = rand()%(boardSize-10)+ 1 ;
                    int end = rand()%(boardSize-start)+start+1;
                    if(board->canAddEntities(start)){
                        board->addBoardEntities(new Ladder(start, end));
                        laddersAdded++;
                    }
                }
            }else{
                for(auto pos : snakePostions){
                    if(board->canAddEntities(pos.first)){
                        board->addBoardEntities(new Snake(pos.first, pos.second));

                    }
                }

                      for(auto pos : ladderPostions){
                    if(board->canAddEntities(pos.first)){
                        board->addBoardEntities(new Ladder(pos.first, pos.second));
                        
                    }
                }
            }
        }

};

class StandardBoardStrategy : public BoardSetupStrategy{
    public:
        void setupBoard(Board* board){
            if(board->getBoardSize()!=100){
                cout<<"Standard Stretegy Only for 10x10" <<endl;
                return;
            }

            board->addBoardEntities(new Snake(99,54));
            board->addBoardEntities(new Snake(95,75));
            board->addBoardEntities(new Snake(92,88));
            board->addBoardEntities(new Snake(89,68));
            board->addBoardEntities(new Snake(74,53));
            board->addBoardEntities(new Snake(64,60));
            board->addBoardEntities(new Snake(62,19));
            board->addBoardEntities(new Snake(49,11));
            board->addBoardEntities(new Snake(46,25));
            board->addBoardEntities(new Snake(16,6));


            board->addBoardEntities(new Ladder(2,38));
            board->addBoardEntities(new Ladder(7,14));
            board->addBoardEntities(new Ladder(8,31));
            board->addBoardEntities(new Ladder(15,26));
            board->addBoardEntities(new Ladder(21,42));
            board->addBoardEntities(new Ladder(28,84));
            board->addBoardEntities(new Ladder(36,44));
            board->addBoardEntities(new Ladder(51,67));
            board->addBoardEntities(new Ladder(71,91));
            board->addBoardEntities(new Ladder(78,98));


        }
};

void Board::setupBoard(BoardSetupStrategy* strategy){
    strategy->setupBoard(this);
}

