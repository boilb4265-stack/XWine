#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace xwine::platform {

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5
};

inline const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "INFO";
}

LogLevel parse_log_level(const std::string& text);

struct LogEntry {
    uint64_t sequence = 0;
    std::string timestamp_iso;
    LogLevel level = LogLevel::Info;
    std::string subsystem;
    std::string message;
};

class Logger {
public:
    static Logger& instance();

    Logger(size_t max_ring_entries = 2000);
    ~Logger();

    void set_min_level(LogLevel level);
    LogLevel min_level() const;

    bool enable_file_sink(const std::filesystem::path& file_path);
    void disable_file_sink();
    void set_stderr_echo(bool enabled);

    void log(LogLevel level, const std::string& subsystem, const std::string& message);

    void trace(const std::string& subsystem, const std::string& message) { log(LogLevel::Trace, subsystem, message); }
    void debug(const std::string& subsystem, const std::string& message) { log(LogLevel::Debug, subsystem, message); }
    void info(const std::string& subsystem, const std::string& message)  { log(LogLevel::Info, subsystem, message); }
    void warn(const std::string& subsystem, const std::string& message)  { log(LogLevel::Warn, subsystem, message); }
    void error(const std::string& subsystem, const std::string& message) { log(LogLevel::Error, subsystem, message); }
    void fatal(const std::string& subsystem, const std::string& message) { log(LogLevel::Fatal, subsystem, message); }

    std::vector<LogEntry> recent_entries(size_t max_count = 200, LogLevel min_filter = LogLevel::Trace) const;
    size_t error_count() const;
    size_t warn_count() const;
    void clear_ring();

    bool export_to_file(const std::filesystem::path& path) const;

private:
    static std::string current_timestamp_iso();

    mutable std::mutex mutex_;
    LogLevel min_level_ = LogLevel::Info;
    size_t max_ring_entries_ = 2000;
    uint64_t sequence_ = 0;
    size_t error_count_ = 0;
    size_t warn_count_ = 0;
    bool stderr_echo_ = false;
    std::deque<LogEntry> ring_;
    std::ofstream file_stream_;
    std::filesystem::path log_file_path_;
};

} // namespace xwine::platform
