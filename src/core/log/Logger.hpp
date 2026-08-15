#pragma once

#include <string>

namespace facial_auth {

// Log severities, mapped onto syslog priorities in Logger::log().
enum class LogLevel { Debug, Info, Warning, Error };

// RAII wrapper around openlog()/syslog()/closelog(). Construct once near
// the top of main() with a process-identifying ident (e.g.
// "facial-auth-verify"); all core code logs through Logger::log().
//
// This is the only acceptable log sink for code that may run inside
// pam_facial.so's exec'd helper — never std::cout/std::cerr, since a
// privileged authentication helper writing to stdout/stderr can surprise
// whatever host process captures those streams.
class Logger {
public:
    explicit Logger(const char* ident);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    static void log(LogLevel level, const std::string& message);
};

}  // namespace facial_auth
