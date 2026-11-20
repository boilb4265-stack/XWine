#include "xwine/platform/logger.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace xwine::platform {

LogLevel parse_log_level(const std::string& text) {
    std::string upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper == "TRACE") return LogLevel::Trace;
    if (upper == "DEBUG") return LogLevel::Debug;
    if (upper == "INFO") return LogLevel::Info;
    if (upper == "WARN" || upper == "WARNING") return LogLevel::Warn;
    if (upper == "ERROR") return LogLevel::Error;
    if (upper == "FATAL") return LogLevel::Fatal;
    return LogLevel::Info;
}

Logger& Logger::instance() {
    static Logger global_logger(4000);
    return global_logger;
}

Logger::Logger(size_t max_ring_entries) : max_ring_entries_(std::max<size_t>(64, max_ring_entries)) {}

Logger::~Logger() {
    disable_file_sink();
}

void Logger::set_min_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

LogLevel Logger::min_level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return min_level_;
}

bool Logger::enable_file_sink(const std::filesystem::path& file_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
    std::error_code ec;
    if (file_path.has_parent_path()) {
        std::filesystem::create_directories(file_path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    file_stream_.open(file_path, std::ios::out | std::ios::app);
    if (!file_stream_.is_open()) {
        return false;
    }
    log_file_path_ = file_path;
    return true;
}

void Logger::disable_file_sink() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.flush();
        file_stream_.close();
    }
}

void Logger::set_stderr_echo(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    stderr_echo_ = enabled;
}

std::string Logger::current_timestamp_iso() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void Logger::log(LogLevel level, const std::string& subsystem, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<uint8_t>(level) < static_cast<uint8_t>(min_level_)) {
        return;
    }

    LogEntry entry;
    entry.sequence = ++sequence_;
    entry.timestamp_iso = current_timestamp_iso();
    entry.level = level;
    entry.subsystem = subsystem;
    entry.message = message;

    if (level == LogLevel::Warn) {
        ++warn_count_;
    } else if (level == LogLevel::Error || level == LogLevel::Fatal) {
        ++error_count_;
    }

    ring_.push_back(entry);
    while (ring_.size() > max_ring_entries_) {
        ring_.pop_front();
    }

    std::ostringstream line;
    line << "[" << entry.timestamp_iso << "] [" << log_level_name(entry.level) << "] ["
         << entry.subsystem << "] " << entry.message << "\n";
    const std::string formatted = line.str();

    if (file_stream_.is_open()) {
        file_stream_ << formatted;
        file_stream_.flush();
    }
    if (stderr_echo_) {
        std::cerr << formatted;
    }
}

std::vector<LogEntry> Logger::recent_entries(size_t max_count, LogLevel min_filter) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> out;
    out.reserve(std::min(max_count, ring_.size()));
    for (auto it = ring_.rbegin(); it != ring_.rend() && out.size() < max_count; ++it) {
        if (static_cast<uint8_t>(it->level) >= static_cast<uint8_t>(min_filter)) {
            out.push_back(*it);
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

size_t Logger::error_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_count_;
}

size_t Logger::warn_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return warn_count_;
}

void Logger::clear_ring() {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.clear();
}

bool Logger::export_to_file(const std::filesystem::path& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
    }
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    for (const auto& e : ring_) {
        out << "[" << e.timestamp_iso << "] [" << log_level_name(e.level) << "] ["
            << e.subsystem << "] " << e.message << "\n";
    }
    return out.good();
}

} // namespace xwine::platform
