#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>
#include <sstream>
#include <fstream>
#include <queue>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace logger
{

enum Severity { debug = 1, info = 2, warrning = 4, error = 8};

namespace {

typedef const std::string& CStrRef;

template<typename T>
using UPtr = std::shared_ptr<T>;


inline std::string fileNameFromPath(std::string path)
{
    return path.substr(path.find_last_of("/\\")+1);
}


std::string getCurrentTimeStamp()
{
    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string strTime = std::ctime(&time);     // std::ctime return string with \n
    if(!strTime.empty()) //without whit check some times throwing error from erase() (size() == 0)
        strTime.erase(strTime.find('\n'), 1);
    return strTime;
}


std::string getStringSeverity(Severity severity)
{
    switch(severity) {
        case Severity::debug: return "debug";
        case Severity::info: return "info";
        case Severity::warrning: return "warning";
        case Severity::error: return "error";
        default: return "Unknown severity";
    }
}

} // namespace


class Logger;

class EntryCollector final
{
public:
    enum class Mode { process, ignore };

private:
    friend Logger;

    EntryCollector(Mode mode = Mode::process, std::function<void(UPtr<std::ostringstream>)> forwardEntry = nullptr);

    EntryCollector(EntryCollector&&) = default;
    EntryCollector& operator =(EntryCollector&&) = default;

public:
    ~EntryCollector();

    template<typename T>
    EntryCollector& operator <<(const T& message);
    EntryCollector& operator <<(std::ostream&(*os)(std::ostream&));

private:
    std::function<void(UPtr<std::ostringstream>)> m_addEntryToQueue;
    UPtr<std::ostringstream> m_entry;
    Mode m_mode;
};


class Logger final
{
public:
    static Logger& getInstance();

    void setLoggedSeverities(int8_t severities);

    void setGlobalLogFileName(CStrRef logFileName);

    EntryCollector log(CStrRef file, uint line, Severity sev);
    EntryCollector slog(CStrRef file, uint line, Severity sev, CStrRef scope);
    EntryCollector loggf(CStrRef file, uint line, Severity sev);
    EntryCollector logf(CStrRef file, uint line, Severity sev, CStrRef logFile);
    EntryCollector sloggf(CStrRef file, uint line, Severity sev, CStrRef scope);
    EntryCollector slogf(CStrRef file, uint line, Severity sev, CStrRef scope, CStrRef logFile);


private:
    Logger();
    ~Logger();

    Logger(Logger&&) = delete;
    Logger(const Logger&) = delete;
    Logger& operator =(Logger&&) = delete;
    Logger& operator =(const Logger&) = delete;

    void addEntryToOSQueue(UPtr<std::ostringstream> entry);
    void addEntryToOFSQueue(const std::string& fileName, UPtr<std::ostringstream> entry);

    EntryCollector createEntryCollector(Severity severity, std::function<void(UPtr<std::ostringstream>)> forwardEntry);

    void processOSEntries();
    void processOFSEntries();

private:
    bool m_isAlive;

    std::mutex m_osMtx;
    std::mutex m_ofsMtx;

    std::thread m_osThread;
    std::thread m_ofsThread;
    std::condition_variable m_osQueueCheck;
    std::condition_variable m_ofsQueueCheck;

    std::string m_logFileName;

    int8_t m_loggedSeverities;
    std::queue<UPtr<std::ostringstream>> m_OSEntries;
    std::queue<std::pair<std::string, UPtr<std::ostringstream>>> m_OFSEntries;
};


#define SET_GLOBAL_LOG_FILE_NAME(fileName) \
        Logger::getInstance().setGlobalLogFileName(fileName);

// simple log
// put message to std::cout
// [file:line] (severity) message
#define LOG(severity) \
            Logger::getInstance().log(__FILE__, __LINE__, severity)


// LOG Global File
// put message to file, that has been set with Logger::setLogFileName
// [file:line] (severity) message 
#define LOGGF(severity) \
        Logger::getInstance().loggf(__FILE__, __LINE__, severity)


// Scoped LOG
// put message to std::cout
// [file:line] (severity) {scope} message
#define SLOG(severity, scope) \
        Logger::getInstance().slog(__FILE__, __LINE__, severity, scope)


// LOG File
// put message to file
// [file:line] (severity) message 
#define LOGF(severity, fileName) \
        Logger::getInstance().logf(__FILE__, __LINE__, severity, fileName)


// Scoped LOG Global File
// scoped log
// put message to file, that has been set with Logger::setLogFileName
// [file:line] (severity) {scope} message
#define SLOGGF(severity, scope) \
        Logger::getInstance().sloggf(__FILE__, __LINE__, severity, scope)


// Scoped LOG File
// put message to file
// [file:line] (severity) {scope} message
#define SLOGF(severity, scope, fileName) \
        Logger::getInstance().slogf(__FILE__, __LINE__, severity, scope, fileName)


EntryCollector::EntryCollector(Mode mode, std::function<void(std::shared_ptr<std::ostringstream>)> forwardEntry)
    : m_addEntryToQueue(forwardEntry),
      m_entry(std::make_shared<std::ostringstream>()),
      m_mode(mode)
{ }


EntryCollector::~EntryCollector()
{
    if(m_mode == Mode::process)
        m_addEntryToQueue(m_entry);
}


template<typename T>
EntryCollector& EntryCollector::operator <<(const T& message)
{
    if(m_mode == Mode::process )
        *m_entry << message;
    
    return *this;
}


EntryCollector& EntryCollector::operator <<(std::ostream&(*os)(std::ostream&))
{
    if(m_mode == Mode::process )
        *m_entry << os;
    
    return *this;
}


Logger& Logger::getInstance()
{
    static Logger logger;
    return logger;
}


void Logger::setLoggedSeverities(int8_t severities)
{
    m_loggedSeverities = severities;
}


void Logger::setGlobalLogFileName(CStrRef logFileName)
{
    m_logFileName = logFileName;
}


Logger::Logger()
    : m_isAlive(true),
      m_logFileName(""),
      m_loggedSeverities(15) // 1111, debug:info:warning:error
{ 
    m_osThread = std::thread([this]{ this->processOSEntries();});
    m_ofsThread = std::thread([this]{ this->processOFSEntries();});
}


Logger::~Logger()
{
    m_isAlive = false;

    m_osQueueCheck.notify_one();
    m_ofsQueueCheck.notify_one();

    m_osThread.join();
    m_ofsThread.join();
}


void Logger::addEntryToOSQueue(std::shared_ptr<std::ostringstream> entry)
{
    std::lock_guard<std::mutex> lg(m_osMtx);
    {
        m_OSEntries.push(entry);
        m_osQueueCheck.notify_one();
    }
}


void Logger::addEntryToOFSQueue(const std::string& fileName, std::shared_ptr<std::ostringstream> entry)
{
    std::lock_guard<std::mutex> lg(m_ofsMtx);
    {
        m_OFSEntries.push(std::make_pair(fileName, entry));
        m_ofsQueueCheck.notify_one();
    }
}


EntryCollector Logger::createEntryCollector(Severity severity, 
                                            std::function<void(std::shared_ptr<std::ostringstream>)> forwardEntry)
{
    if((m_loggedSeverities & severity) != severity)
        return EntryCollector(EntryCollector::Mode::ignore);

    return EntryCollector(EntryCollector::Mode::process, forwardEntry);
}


void Logger::processOSEntries()
{
    while(m_isAlive) {
        std::unique_lock<std::mutex> locker(m_osMtx);
        {
            m_osQueueCheck.wait(locker, [&]{ return !m_OSEntries.empty() || !m_isAlive; });

            while(!m_OSEntries.empty()) {
                if(m_OSEntries.front())
                    std::cout << m_OSEntries.front()->str();

                m_OSEntries.pop();
            }
        }
    }
}


void Logger::processOFSEntries()
{
    while(m_isAlive) {
        std::unique_lock<std::mutex> locker(m_ofsMtx);
        {
            m_ofsQueueCheck.wait(locker, [&]{ return !m_OFSEntries.empty() || !m_isAlive; });

            while(!m_OFSEntries.empty()) {
                if(m_OFSEntries.front().first != "" && m_OFSEntries.front().second) {
                    std::ofstream logStream(m_OFSEntries.front().first, std::ios::out | std::ios::app);
                    logStream << m_OFSEntries.front().second->str();
                }
                m_OFSEntries.pop();
            }
        }
    }
}


EntryCollector Logger::log(CStrRef fileName, uint line, Severity severity)
{
    auto ec = createEntryCollector(severity, [this](auto entry){ this->addEntryToOSQueue(entry); });
    
    ec << '<' << getCurrentTimeStamp() << '>'
       << '[' << fileNameFromPath(fileName) << ":" << line << ']'
       << "( " << getStringSeverity(severity) << " ) ";
    
    return ec;
}


EntryCollector Logger::slog(CStrRef fileName, uint line, Severity severity, CStrRef scope)
{
    auto ec = log(fileName, line, severity);

    ec << "{ " << scope << " } ";
    
    return ec;
}


EntryCollector Logger::logf(CStrRef fileName, uint line, Severity severity, CStrRef logFileName)
{
    auto addEntryFunc = [this] (const std::string& logFileName, auto entry) { 
        this->addEntryToOFSQueue(logFileName, entry);
    };
 
    auto ec = createEntryCollector(severity, std::bind(addEntryFunc, logFileName, std::placeholders::_1));

    ec << '<' << getCurrentTimeStamp() << '>'
       << '[' << fileNameFromPath(fileName) << ":" << line << ']'
       << "( " << getStringSeverity(severity) << " ) ";

    return ec;
}


EntryCollector Logger::loggf(CStrRef fileName, uint line, Severity severity)
{
    return logf(fileName, line, severity, m_logFileName);
}


EntryCollector Logger::slogf(CStrRef fileName, uint line, Severity severity, CStrRef scope, CStrRef logFileName)
{
    auto ec = logf(fileName, line, severity, logFileName);

    ec << "{ " << scope << " } ";
    
    return ec;
}


EntryCollector Logger::sloggf(CStrRef fileName, uint line, Severity severity, CStrRef scope)
{
    auto ec = logf(fileName, line, severity, m_logFileName);

    ec << "{ " << scope << " } ";
    
    return ec;
}


} // namespace logger

#endif //LOGGER_HPP