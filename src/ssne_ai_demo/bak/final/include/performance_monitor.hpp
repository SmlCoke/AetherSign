#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using PerfClock = std::chrono::steady_clock;

double PerfDurationMs(const PerfClock::time_point& begin,
                      const PerfClock::time_point& end);

struct PerfSample {
    uint32_t frame_index = 0;
    uint32_t sample_index = 0;
    double elapsed_ms = 0.0;
    double frame_period_ms = 0.0;
    double e2e_ms = 0.0;
    double capture_ms = 0.0;
    double palm_ms = 0.0;
    double palm_pre_ms = 0.0;
    double palm_infer_ms = 0.0;
    double palm_post_ms = 0.0;
    double hand_ms = 0.0;
    double gloss_ms = 0.0;
    double osd_ms = 0.0;
    double osd_clear_ms = 0.0;
    double osd_palm_ms = 0.0;
    double osd_hand_ms = 0.0;
    double osd_flush_ms = 0.0;
    double osd_texture_ms = 0.0;
};

class PerformanceMonitor {
public:
    PerformanceMonitor(const std::string& mode_name,
                       uint32_t print_interval,
                       float sensor_fps);

    void Start(uint32_t warmup_frames);
    void Record(const PerfSample& sample);
    void RecordCaptureFailure();
    void PrintRawReport(uint32_t rendered_frames) const;

private:
    enum MetricIndex {
        kFramePeriod = 0,
        kE2e,
        kCapture,
        kPalm,
        kPalmPre,
        kPalmInfer,
        kPalmPost,
        kHand,
        kGloss,
        kOsd,
        kOsdClear,
        kOsdPalm,
        kOsdHand,
        kOsdFlush,
        kOsdTexture,
        kMetricCount,
    };

    struct MetricAccumulator {
        uint32_t count = 0;
        std::vector<uint32_t> bins;

        MetricAccumulator();
        void Reset();
        void Add(double value_ms);
    };

    void PrintSample(const PerfSample& sample) const;
    void PrintHistogram(MetricIndex index) const;

    std::string mode_;
    uint32_t print_interval_;
    float sensor_fps_;
    PerfClock::time_point start_time_;
    uint32_t capture_failures_ = 0;
    uint32_t sample_count_ = 0;
    std::vector<MetricAccumulator> metrics_;
};
