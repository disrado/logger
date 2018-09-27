#include "logger/Logger.hpp"


namespace logger
{


EntryCollector::EntryCollector(Mode mode, ForwardEntryFunc forwardEntry)
	: m_addEntryToQueue(forwardEntry)
	, m_entry(std::make_shared<std::ostringstream>())
	, m_mode(mode)
{ }


EntryCollector::~EntryCollector()
{
	if (m_mode == Mode::process) {
		m_addEntryToQueue(m_entry);
	}
}


EntryCollector& EntryCollector::operator<<(std::ostream&(*os)(std::ostream&))
{
	if(m_mode == Mode::process ) {
		*m_entry << os;
	}
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


void Logger::setGlobalLogFile(CStrRef logFileName)
{
	m_logFileName = logFileName;
}


Logger::Logger()
	: m_isAlive(true)
	, m_logFileName("")
	, m_loggedSeverities(15) // 1111, debug:info:warning:error
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


void Logger::addEntryToOSQueue(ShPtr<std::ostringstream> entry)
{
	{ const std::lock_guard<std::mutex> lg(m_osMtx);
		m_OSEntries.push(entry);
		m_osQueueCheck.notify_one();
	}
}


void Logger::addEntryToOFSQueue(const std::string& fileName, ShPtr<std::ostringstream> entry)
{
	{ const std::lock_guard<std::mutex> lg(m_ofsMtx);
		m_OFSEntries.push(std::make_pair(fileName, entry));
		m_ofsQueueCheck.notify_one();
	}
}


EntryCollector Logger::createEntryCollector(Severity severity, ForwardEntryFunc forwardEntry)
{
	if ((m_loggedSeverities & severity) != severity) {
		return EntryCollector(EntryCollector::Mode::ignore);
	}
	return EntryCollector(EntryCollector::Mode::process, forwardEntry);
}


void Logger::processOSEntries()
{
	while (m_isAlive) {
		{ std::unique_lock<std::mutex> locker(m_osMtx);
			m_osQueueCheck.wait(locker, [&] { return !m_OSEntries.empty() || !m_isAlive; });

			while (!m_OSEntries.empty()) {
				if (m_OSEntries.front()) {
					std::cout << m_OSEntries.front()->str();
				}
				m_OSEntries.pop();
			}
		}
	}
}


void Logger::processOFSEntries()
{
	while (m_isAlive) {
		{ std::unique_lock<std::mutex> locker(m_ofsMtx);
			m_ofsQueueCheck.wait(locker, [&] { return !m_OFSEntries.empty() || !m_isAlive; });

			while (!m_OFSEntries.empty()) {
				if (m_OFSEntries.front().first != "" && m_OFSEntries.front().second) {
					std::ofstream logStream{ 
						m_OFSEntries.front().first,
						std::ios::out | std::ios::app };
						
					logStream << m_OFSEntries.front().second->str();
				}
				m_OFSEntries.pop();
			}
		}
	}
}


EntryCollector Logger::log(CStrRef fileName, uint line, Severity severity)
{
	auto ec{ createEntryCollector(severity, [this] (auto entry) { 
		this->addEntryToOSQueue(entry); }) 
	};
	
	ec << '<' << getCurrentTimeStamp() << '>'
	   << '[' << fileNameFromPath(fileName) << ":" << line << ']'
	   << "( " << getStringSeverity(severity) << " ) ";
	
	return ec;
}


EntryCollector Logger::slog(CStrRef fileName, uint line, Severity severity, CStrRef scope)
{
	auto ec{ log(fileName, line, severity) };

	ec << "{ " << scope << " } ";
	
	return ec;
}


EntryCollector Logger::logf(CStrRef fileName, uint line, Severity severity, CStrRef logFileName)
{
	auto addEntryFunc{ [this] (const std::string& logFileName, auto entry) { 
		this->addEntryToOFSQueue(logFileName, entry);
	} };
 
	auto ec{
		createEntryCollector(
			severity,
			std::bind(addEntryFunc, logFileName, std::placeholders::_1)) };

	ec << '<' << getCurrentTimeStamp() << '>'
	   << '[' << fileNameFromPath(fileName) << ":" << line << ']'
	   << "( " << getStringSeverity(severity) << " ) ";

	return ec;
}


EntryCollector Logger::loggf(CStrRef fileName, uint line, Severity severity)
{
	return logf(fileName, line, severity, m_logFileName);
}


EntryCollector Logger::slogf(
	CStrRef fileName,
	uint line,
	Severity severity,
	CStrRef scope,
	CStrRef logFileName)
{
	auto ec{ logf(fileName, line, severity, logFileName) };

	ec << "{ " << scope << " } ";
	
	return ec;
}


EntryCollector Logger::sloggf(CStrRef fileName, uint line, Severity severity, CStrRef scope)
{
	auto ec{ logf(fileName, line, severity, m_logFileName) };

	ec << "{ " << scope << " } ";
	
	return ec;
}


} // namespace logger