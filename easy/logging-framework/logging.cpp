#include <bits/stdc++.h>
using namespace std;

enum class LogLevel {
    TRACE,
    ERROR,
    WARN,
    INFO,
    DEBUG
};

string toString(LogLevel level){
        switch(level){
            case LogLevel :: TRACE : return "TRACE";
            case LogLevel :: ERROR : return "ERROR";
            case LogLevel :: WARN : return "WARN";
            case LogLevel :: INFO : return "INFO";
            case LogLevel :: DEBUG : return "DEBUG";
            default: return "UNKNOWN";
        }
}

class LogMessage{
    private: 
        LogLevel level;
        string message;
        string source;
        string timestamp; 

    public:
        LogMessage(LogLevel level, const string& message, const string& source){
            this->level = level;
            this->message = message;
            this->source = source;

            time_t now = time(nullptr);
            ostringstream oss;
            oss << put_time(localtime(&now), "%Y-%-m%-d %H:%M:%S");
            this->timestamp = oss.str();
        } 

        string getFormattedMessage() const{
            return "[" + timestamp + "] [" + toString(level) + "] [" + source + "] " + message;
        }

};

class LogAppender{
    public:
        virtual ~LogAppender(){}
        virtual void append(const LogMessage& message) = 0; 
};

class ConsoleAppender : public LogAppender{
    public:
        void append(const LogMessage& message){
            cout << message.getFormattedMessage() << endl;
        }
};

class FileAppender : public LogAppender{
    private:
        ofstream file;
    public:
        FileAppender(const string& filename){
            file.open(filename, ios::app);
        }
        ~ FileAppender(){
            if(file.is_open()){
                file.close();
            }
        }
        void append(const LogMessage& message){
            if(file.is_open()){
                file << message.getFormattedMessage() << endl;
            }
        }
};


class Logger{
    private:
        string name;
        LogLevel minLevel;
        vector<shared_ptr<LogAppender>> appenders;

        bool isEnabled(LogLevel level){
            return static_cast<int>(level) >= static_cast<int>(minLevel) ;
        }

    public: 
        Logger(const string& name, LogLevel minLevel = LogLevel::INFO){
            this->name = name;
            this->minLevel = minLevel;
        }

        void addAppender(shared_ptr<LogAppender> appender){
            appenders.push_back(appender);
        }

        void setMinLevel(LogLevel level){
            minLevel = level;
        }

        void log(LogLevel level, const string& message){
            if(!isEnabled(level)){
                return;
            }

            LogMessage record(level, message, name);
            for(const auto& appender: appenders){
                appender->append(record);
            }
        }

        void trace(const string& message) { log(LogLevel::TRACE, message); }
        void debug(const string& message) { log(LogLevel::DEBUG, message); }
        void info (const string& message) { log(LogLevel::INFO,  message); }
        void warn (const string& message) { log(LogLevel::WARN,  message); }
        void error(const string& message) { log(LogLevel::ERROR, message); }
    };



int main(){

    Logger logger("MyApp");
    logger.addAppender(make_shared<ConsoleAppender>());
    logger.addAppender(make_shared<FileAppender>("app.log"));

    logger.debug("filtered out, below INFO");
    logger.info ("Application started");
    logger.warn ("Low disk space");
    logger.error("An error occurred");

    logger.setMinLevel(LogLevel::DEBUG);
    logger.debug("Now visible");

    return 0;
}