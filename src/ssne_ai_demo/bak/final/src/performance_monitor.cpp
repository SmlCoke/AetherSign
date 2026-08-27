#include "../include/performance_monitor.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

const uint32_t kHistogramBinUs = 250;
const uint32_t kHistogramBinCount = 2048;
const size_t kMaxHistogramPayloadChars = 640;

const char* const kMetricNames[] = {
    "frame_period_ms", "e2e_ms", "cap_ms", "palm_ms", "palm_pre_ms",
    "palm_infer_ms", "palm_post_ms", "hand_ms", "gloss_ms", "osd_ms",
    "osd_clear_ms", "osd_palm_ms", "osd_hand_ms", "osd_flush_ms",
    "osd_texture_ms",
};

}  // namespace

double PerfDurationMs(const PerfClock::time_point& begin,
                      const PerfClock::time_point& end) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - begin)
        .count();
}

PerformanceMonitor::MetricAccumulator::MetricAccumulator() {}

void PerformanceMonitor::MetricAccumulator::Reset() {
    count = 0;
    if (bins.size() != kHistogramBinCount) {
        bins.assign(kHistogramBinCount, 0);
    } else {
        std::fill(bins.begin(), bins.end(), 0);
    }
}

void PerformanceMonitor::MetricAccumulator::Add(double value_ms) {
    if (!std::isfinite(value_ms) || value_ms < 0.0) {
        value_ms = 0.0;
    }
    count += 1;

    const double bin_width_ms = static_cast<double>(kHistogramBinUs) / 1000.0;
    size_t bin = static_cast<size_t>(std::floor(value_ms / bin_width_ms + 0.5));
    bin = std::min(bin, bins.size() - 1);
    bins[bin] += 1;
}

PerformanceMonitor::PerformanceMonitor(const std::string& mode_name,
                                       uint32_t print_interval,
                                       float sensor_fps)
    : mode_(mode_name),
      print_interval_(print_interval),
      sensor_fps_(sensor_fps > 0.0f ? sensor_fps : 30.0f),
      start_time_(PerfClock::now()),
      metrics_(kMetricCount) {}

void PerformanceMonitor::Start(uint32_t warmup_frames) {
    start_time_ = PerfClock::now();
    capture_failures_ = 0;
    sample_count_ = 0;
    for (size_t i = 0; i < metrics_.size(); i++) {
        metrics_[i].Reset();
    }

    std::cout << std::fixed << std::setprecision(3)
              << "[PERF_BEGIN] mode=" << mode_
              << " format=raw_time_hist_v2"
              << " sensor_fps=" << sensor_fps_
              << " print_interval=" << print_interval_
              << " warmup_frames=" << warmup_frames
              << " hist_bin_us=" << kHistogramBinUs
              << " hist_bins=" << kHistogramBinCount
              << std::endl;
}

void PerformanceMonitor::Record(const PerfSample& input_sample) {
    PerfSample sample = input_sample;
    sample_count_ += 1;
    sample.sample_index = sample_count_;
    sample.elapsed_ms = PerfDurationMs(start_time_, PerfClock::now());

    metrics_[kFramePeriod].Add(sample.frame_period_ms);
    metrics_[kE2e].Add(sample.e2e_ms);
    metrics_[kCapture].Add(sample.capture_ms);
    metrics_[kPalm].Add(sample.palm_ms);
    metrics_[kPalmPre].Add(sample.palm_pre_ms);
    metrics_[kPalmInfer].Add(sample.palm_infer_ms);
    metrics_[kPalmPost].Add(sample.palm_post_ms);
    metrics_[kHand].Add(sample.hand_ms);
    metrics_[kGloss].Add(sample.gloss_ms);
    metrics_[kOsd].Add(sample.osd_ms);
    metrics_[kOsdClear].Add(sample.osd_clear_ms);
    metrics_[kOsdPalm].Add(sample.osd_palm_ms);
    metrics_[kOsdHand].Add(sample.osd_hand_ms);
    metrics_[kOsdFlush].Add(sample.osd_flush_ms);
    metrics_[kOsdTexture].Add(sample.osd_texture_ms);

    if (print_interval_ > 0 && sample.sample_index % print_interval_ == 0) {
        PrintSample(sample);
    }
}

void PerformanceMonitor::RecordCaptureFailure() {
    capture_failures_ += 1;
}

void PerformanceMonitor::PrintSample(const PerfSample& sample) const {
    std::cout << std::fixed << std::setprecision(3)
              << "[PERF] mode=" << mode_
              << " sample=" << sample.sample_index
              << " frame=" << sample.frame_index
              << " elapsed_ms=" << sample.elapsed_ms
              << " frame_period_ms=" << sample.frame_period_ms
              << " e2e_ms=" << sample.e2e_ms
              << " cap_ms=" << sample.capture_ms
              << " palm_ms=" << sample.palm_ms
              << " palm_pre_ms=" << sample.palm_pre_ms
              << " palm_infer_ms=" << sample.palm_infer_ms
              << " palm_post_ms=" << sample.palm_post_ms
              << " hand_ms=" << sample.hand_ms
              << " gloss_ms=" << sample.gloss_ms
              << " osd_ms=" << sample.osd_ms
              << " osd_clear_ms=" << sample.osd_clear_ms
              << " osd_palm_ms=" << sample.osd_palm_ms
              << " osd_hand_ms=" << sample.osd_hand_ms
              << " osd_flush_ms=" << sample.osd_flush_ms
              << " osd_texture_ms=" << sample.osd_texture_ms
              << std::endl;
}

void PerformanceMonitor::PrintHistogram(MetricIndex index) const {
    const MetricAccumulator& metric = metrics_[index];
    std::vector<std::string> chunks;
    std::ostringstream chunk;

    for (size_t bin = 0; bin < metric.bins.size(); bin++) {
        if (metric.bins[bin] == 0) {
            continue;
        }
        std::ostringstream token;
        token << bin << ":" << metric.bins[bin];
        const std::string value = token.str();
        if (chunk.tellp() > 0 &&
            static_cast<size_t>(chunk.tellp()) + value.size() + 1 >
                kMaxHistogramPayloadChars) {
            chunks.push_back(chunk.str());
            chunk.str("");
            chunk.clear();
        }
        if (chunk.tellp() > 0) {
            chunk << ",";
        }
        chunk << value;
    }
    if (chunk.tellp() > 0 || chunks.empty()) {
        chunks.push_back(chunk.str());
    }

    for (size_t part = 0; part < chunks.size(); part++) {
        std::cout << std::fixed << std::setprecision(6)
                  << "[PERF_RAW_HIST] mode=" << mode_
                  << " metric=" << kMetricNames[index]
                  << " bin_us=" << kHistogramBinUs
                  << " hist_bins=" << kHistogramBinCount
                  << " count=" << metric.count
                  << " part=" << part
                  << " last=" << (part + 1 == chunks.size() ? 1 : 0)
                  << " bins=" << chunks[part]
                  << std::endl;
    }
}

void PerformanceMonitor::PrintRawReport(uint32_t rendered_frames) const {
    const double elapsed_ms = PerfDurationMs(start_time_, PerfClock::now());
    std::cout << std::fixed << std::setprecision(6)
              << "[PERF_RAW_META] mode=" << mode_
              << " format=raw_time_hist_v2"
              << " samples=" << sample_count_
              << " frames=" << rendered_frames
              << " capture_failures=" << capture_failures_
              << " elapsed_ms=" << elapsed_ms
              << " fps_sensor=" << sensor_fps_
              << " hist_bin_us=" << kHistogramBinUs
              << " hist_bins=" << kHistogramBinCount
              << std::endl;
    for (int index = 0; index < kMetricCount; index++) {
        PrintHistogram(static_cast<MetricIndex>(index));
    }
    std::cout << "[PERF_RAW_END] mode=" << mode_
              << " samples=" << sample_count_
              << " status=complete"
              << std::endl;
}
