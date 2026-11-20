#include "xwine/platform/benchmark.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace xwine::platform {

BenchmarkTracker::BenchmarkTracker(size_t window_size)
    : window_size_(std::max<size_t>(16, window_size)) {}

void BenchmarkTracker::record_frame_ms(double frame_ms) {
    if (!std::isfinite(frame_ms) || frame_ms < 0.0) {
        return;
    }
    frame_history_.push_back(frame_ms);
    while (frame_history_.size() > window_size_) {
        frame_history_.pop_front();
    }
    ++total_frames_;
}

void BenchmarkTracker::record_load_timing(const std::string& label, double duration_ms) {
    if (!std::isfinite(duration_ms) || duration_ms < 0.0) {
        return;
    }
    load_timings_.push_back({label, duration_ms});
    if (load_timings_.size() > 64) {
        load_timings_.erase(load_timings_.begin());
    }
}

void BenchmarkTracker::record_crash_or_fault(const std::string& description) {
    faults_.push_back(description);
}

FrameStats BenchmarkTracker::frame_stats() const {
    FrameStats stats;
    stats.total_frames = total_frames_;
    if (frame_history_.empty()) {
        return stats;
    }

    std::vector<double> sorted(frame_history_.begin(), frame_history_.end());
    std::sort(sorted.begin(), sorted.end());

    const double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    stats.avg_frame_ms = sum / static_cast<double>(sorted.size());
    stats.min_frame_ms = sorted.front();
    stats.max_frame_ms = sorted.back();

    auto percentile = [&sorted](double p) -> double {
        if (sorted.size() == 1) return sorted[0];
        const double idx = p * static_cast<double>(sorted.size() - 1);
        const size_t lo = static_cast<size_t>(std::floor(idx));
        const size_t hi = std::min(sorted.size() - 1, lo + 1);
        const double frac = idx - static_cast<double>(lo);
        return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
    };

    stats.p95_frame_ms = percentile(0.95);
    stats.p99_frame_ms = percentile(0.99);
    if (stats.avg_frame_ms > 0.0001) {
        stats.current_fps = 1000.0 / stats.avg_frame_ms;
    }
    return stats;
}

void BenchmarkTracker::reset() {
    frame_history_.clear();
    total_frames_ = 0;
    load_timings_.clear();
    faults_.clear();
}

} // namespace xwine::platform
