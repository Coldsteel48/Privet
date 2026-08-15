#include "Logger.hpp"

#include <syslog.h>

namespace facial_auth {

namespace {

int toSyslogPriority(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return LOG_DEBUG;
        case LogLevel::Info:
            return LOG_INFO;
        case LogLevel::Warning:
            return LOG_WARNING;
        case LogLevel::Error:
            return LOG_ERR;
    }
    return LOG_INFO;
}

}  // namespace

Logger::Logger(const char* ident) {
    openlog(ident, LOG_PID, LOG_AUTHPRIV);
}

Logger::~Logger() {
    closelog();
}

void Logger::log(LogLevel level, const std::string& message) {
    syslog(toSyslogPriority(level), "%s", message.c_str());
}

}  // namespace facial_auth
