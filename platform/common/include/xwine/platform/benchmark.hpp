#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace xwine::platform {

struct FrameStats {
    double current_fps = 0.0;
    double avg_frame_ms = 0.0;
    double min_frame_ms = 0.0;
    double max_frame_ms = 0.0;
    double p95_frame_ms = 0.0;
    double p99_frame_ms = 0.0;
    uint64_t total_frames = 0;
};

struct LoadTimingRecord {
    std::string label;
    double duration_ms = 0.0;
};

class BenchmarkTracker {
public:
    explicit BenchmarkTracker(size_t window_size = 240);

    void record_frame_ms(double frame_ms);
    void record_load_timing(const std::string& label, double duration_ms);
    void record_crash_or_fault(const std::string& description);

    FrameStats frame_stats() const;
    const std::deque<double>& frame_history_ms() const { return frame_history_; }
    const std::vector<LoadTimingRecord>& load_timings() const { return load_timings_; }
    const std::vector<std::string>& recorded_faults() const { return faults_; }

    void reset();

private:
    size_t window_size_;
    std::deque<double> frame_history_;
    uint64_t total_frames_ = 0;
    std::vector<LoadTimingRecord> load_timings_;
    std::vector<std::string> faults_;
};

class ScopedTimer {
public:
    explicit ScopedTimer(BenchmarkTracker& tracker, std::string label)
        : tracker_(tracker), label_(std::move(label)), start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        const auto end = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(end - start_).count();
        tracker_.record_load_timing(label_, ms);
    }

private:
    BenchmarkTracker& tracker_;
    std::string label_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace xwine::platform
