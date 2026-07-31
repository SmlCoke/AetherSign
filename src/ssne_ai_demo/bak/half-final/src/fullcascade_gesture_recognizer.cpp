#include "../include/common.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace {

const float kSideValue[kGestureNumHands] = {-1.0f, 1.0f};

std::string ArrayToString(const std::array<float, kGestureNumClasses>& values) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << "[";
    for (int i = 0; i < kGestureNumClasses; i++) {
        if (i > 0) {
            oss << ",";
        }
        oss << values[i];
    }
    oss << "]";
    return oss.str();
}

}  // namespace

void GestureResult::Clear() {
    logits.fill(0.0f);
    probabilities.fill(0.0f);
    top_index = -1;
    stable_index = -1;
    stable_count = 0;
    buffered_frames = 0;
    valid_frames_in_window = 0;
    predict_count = 0;
    ready = false;
    valid = false;
}

void FULLCASCADEGESTURERECOGNIZER::RawFrame::Clear() {
    for (int side = 0; side < kGestureNumHands; side++) {
        for (int point = 0; point < kFullCascadePointsPerHand; point++) {
            values[side][point][0] = 0.0f;
            values[side][point][1] = 0.0f;
            values[side][point][2] = 0.0f;
        }
    }
}

FULLCASCADEGESTURERECOGNIZER::FULLCASCADEGESTURERECOGNIZER()
    : ring_buffer(kFullCascadeWindowFrames),
      input_buffer(kFullCascadeInputElements, 0.0f),
      input_buffer_int8(kFullCascadeInputElements, 0),
      input_buffer_uint8(kFullCascadeInputElements, 0) {
    input.data = nullptr;
    inputs[0].data = nullptr;
    outputs[0].data = nullptr;
}

void FULLCASCADEGESTURERECOGNIZER::Initialize(
    const std::string& model_path,
    const std::array<int, 2>& in_image_shape,
    bool in_rotate_features_clockwise,
    GestureInputTensorMode in_input_tensor_mode,
    uint32_t in_min_valid_frames,
    float in_input_quant_scale,
    float in_min_palm_score,
    float in_min_hand_score,
    uint32_t in_warmup_frames,
    uint32_t in_debug_interval,
    uint32_t in_stable_hits,
    bool in_verbose_log,
    uint32_t in_no_input_reset_frames) {
    image_shape = in_image_shape;
    rotate_features_clockwise = in_rotate_features_clockwise;
    input_tensor_mode = in_input_tensor_mode;
    min_valid_frames =
        std::max<uint32_t>(1, std::min<uint32_t>(in_min_valid_frames, kFullCascadeWindowFrames));
    input_quant_scale = in_input_quant_scale > 0.0f ? in_input_quant_scale : 64.0f;
    min_palm_score = in_min_palm_score;
    min_hand_score = in_min_hand_score;
    warmup_frames =
        std::max<uint32_t>(1, std::min<uint32_t>(in_warmup_frames, kFullCascadeWindowFrames));
    debug_interval = std::max<uint32_t>(1, in_debug_interval);
    stable_hits = std::max<uint32_t>(1, in_stable_hits);
    verbose_log = in_verbose_log;
    no_input_reset_frames = in_no_input_reset_frames;

    feature_shape = rotate_features_clockwise
                        ? std::array<int, 2>{{image_shape[1], image_shape[0]}}
                        : image_shape;

    ring_buffer.assign(kFullCascadeWindowFrames, RawFrame());
    input_buffer.assign(kFullCascadeInputElements, 0.0f);
    input_buffer_int8.assign(kFullCascadeInputElements, 0);
    input_buffer_uint8.assign(kFullCascadeInputElements, 0);
    write_index = 0;
    frames_seen = 0;
    predict_count = 0;
    consecutive_no_input_frames = 0;
    last_top_index = -1;
    stable_count = 0;
    consecutive_inference_failures = 0;
    output_info_logged = false;

    char* model_path_char = const_cast<char*>(model_path.c_str());
    model_id = ssne_loadmodel(model_path_char, SSNE_STATIC_ALLOC);
    model_input_dtype = -1;
    ssne_get_model_input_dtype(model_id, &model_input_dtype);
    if (model_input_dtype == SSNE_INT8 || model_input_dtype == SSNE_UINT8 ||
        model_input_dtype == SSNE_FLOAT32) {
        runtime_input_dtype = model_input_dtype;
    } else {
        runtime_input_dtype = SSNE_FLOAT32;
    }

    if (input_tensor_mode == kGestureInputTensorModeFlatBytes) {
        input = create_tensor(static_cast<uint32_t>(kFullCascadeInputElements),
                              1,
                              SSNE_BYTES,
                              SSNE_BUF_AI);
    } else {
        input = create_tensor(static_cast<uint32_t>(kFullCascadeInputTensorWidth),
                              static_cast<uint32_t>(kFullCascadeInputTensorHeight),
                              SSNE_BYTES,
                              SSNE_BUF_AI);
    }
    const int set_dtype_ret = set_data_type(input, static_cast<uint8_t>(runtime_input_dtype));
    inputs[0] = input;

    PrintInitializeLog(model_path);
    if (set_dtype_ret != 0) {
        std::cerr << "[FULLCASCADE] set_data_type(runtime_input_dtype=" << runtime_input_dtype
                  << ") failed, ret=" << set_dtype_ret << std::endl;
    }

    initialized = true;
}

void FULLCASCADEGESTURERECOGNIZER::UpdateAndPredict(const PalmResult& palm_result,
                                                    const HandResult& hand_result,
                                                    uint32_t frame_index,
                                                    GestureResult* result) {
    if (result == nullptr) {
        return;
    }
    result->Clear();
    result->buffered_frames = std::min<uint32_t>(frames_seen, kFullCascadeWindowFrames);
    result->predict_count = predict_count;

    if (!initialized) {
        std::cerr << "[FULLCASCADE] UpdateAndPredict called before Initialize." << std::endl;
        return;
    }

    AppendFrame(palm_result, hand_result);
    result->buffered_frames = std::min<uint32_t>(frames_seen, kFullCascadeWindowFrames);
    result->ready = result->buffered_frames >= warmup_frames;
    if (!result->ready) {
        if (frame_index % debug_interval == 0) {
            std::cout << "[FULLCASCADE][frame " << frame_index << "] waiting_for_window buffered="
                      << result->buffered_frames << "/" << warmup_frames
                      << ", palm=" << palm_result.detections.size()
                      << ", hand=" << hand_result.detections.size()
                      << std::endl;
        }
        return;
    }

    uint32_t valid_frames_in_window = 0;
    BuildModelInput(&valid_frames_in_window);
    result->valid_frames_in_window = valid_frames_in_window;
    const FeatureStats stats = GetFeatureStats(input_buffer);
    if (valid_frames_in_window < min_valid_frames) {
        if (frame_index % debug_interval == 0) {
            std::cout << "[FULLCASCADE][frame " << frame_index
                      << "] skip_empty_window valid_frames=" << valid_frames_in_window
                      << "/" << min_valid_frames
                      << ", buffered=" << result->buffered_frames
                      << ", palm=" << palm_result.detections.size()
                      << ", hand=" << hand_result.detections.size()
                      << ", input_nonzero=" << stats.nonzero_count << "/" << input_buffer.size()
                      << std::endl;
        }
        return;
    }

    if (!RunInference(result)) {
        return;
    }

    predict_count += 1;
    result->predict_count = predict_count;
    if (result->top_index == last_top_index) {
        stable_count += 1;
    } else {
        last_top_index = result->top_index;
        stable_count = 1;
    }
    result->stable_count = stable_count;
    result->stable_index = stable_count >= stable_hits ? last_top_index : -1;

    if (predict_count % debug_interval == 0 || stable_count == 1 ||
        result->stable_count == stable_hits) {
        PrintPredictLog(frame_index, *result, stats, palm_result, hand_result);
    }
}

void FULLCASCADEGESTURERECOGNIZER::Release() {
    if (input.data != nullptr) {
        release_tensor(input);
        input.data = nullptr;
        inputs[0].data = nullptr;
    }

    if (outputs[0].data != nullptr) {
        release_tensor(outputs[0]);
        outputs[0].data = nullptr;
    }

    initialized = false;
}

const char* FULLCASCADEGESTURERECOGNIZER::ClassName(int index) {
    switch (index) {
        case 0:
            return "rain";
        case 1:
            return "long";
        case 2:
            return "short";
        case 3:
            return "go";
        case 4:
            return "thick";
        default:
            return "unknown";
    }
}

float FULLCASCADEGESTURERECOGNIZER::Clamp(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

size_t FULLCASCADEGESTURERECOGNIZER::DTypeElementBytes(int dtype) {
    return dtype == SSNE_FLOAT32 ? sizeof(float) : sizeof(uint8_t);
}

FULLCASCADEGESTURERECOGNIZER::TensorDebugInfo
FULLCASCADEGESTURERECOGNIZER::GetTensorDebugInfo(ssne_tensor_t tensor) {
    TensorDebugInfo info;
    info.width = get_width(tensor);
    info.height = get_height(tensor);
    info.dtype = get_data_type(tensor);
    info.format = get_data_format(tensor);
    info.mem_size = get_mem_size(tensor);
    info.total_size = get_total_size(tensor);
    info.inferred_elements = InferElementCount(info);
    return info;
}

size_t FULLCASCADEGESTURERECOGNIZER::InferElementCount(const TensorDebugInfo& info) {
    if (info.dtype == SSNE_FLOAT32) {
        return info.mem_size / sizeof(float);
    }
    if (info.dtype == SSNE_UINT8 || info.dtype == SSNE_INT8) {
        return info.mem_size;
    }
    return info.total_size;
}

float FULLCASCADEGESTURERECOGNIZER::ReadTensorValue(ssne_tensor_t tensor,
                                                    const TensorDebugInfo& info,
                                                    size_t index) {
    const void* data = get_data(tensor);
    if (data == nullptr || index >= info.inferred_elements) {
        return 0.0f;
    }

    if (info.dtype == SSNE_FLOAT32) {
        const float* values = reinterpret_cast<const float*>(data);
        return values[index];
    }
    if (info.dtype == SSNE_UINT8) {
        const uint8_t* values = reinterpret_cast<const uint8_t*>(data);
        return static_cast<float>(values[index]);
    }
    if (info.dtype == SSNE_INT8) {
        const int8_t* values = reinterpret_cast<const int8_t*>(data);
        return static_cast<float>(values[index]);
    }
    return 0.0f;
}

FULLCASCADEGESTURERECOGNIZER::FeatureStats
FULLCASCADEGESTURERECOGNIZER::GetFeatureStats(const std::vector<float>& values) {
    FeatureStats stats;
    if (values.empty()) {
        return stats;
    }

    stats.min_value = std::numeric_limits<float>::max();
    stats.max_value = std::numeric_limits<float>::lowest();
    double sum = 0.0;
    for (size_t i = 0; i < values.size(); i++) {
        const float value = values[i];
        stats.min_value = std::min(stats.min_value, value);
        stats.max_value = std::max(stats.max_value, value);
        sum += static_cast<double>(value);
        stats.nonzero_count += value != 0.0f ? 1 : 0;
    }
    stats.mean_value = sum / static_cast<double>(values.size());
    return stats;
}

bool FULLCASCADEGESTURERECOGNIZER::RawFrameHasInput(const RawFrame& frame) {
    for (int side = 0; side < kGestureNumHands; side++) {
        for (int point = 0; point < kFullCascadePointsPerHand; point++) {
            if (frame.values[side][point][2] > 0.5f) {
                return true;
            }
        }
    }
    return false;
}

void FULLCASCADEGESTURERECOGNIZER::AppendFrame(const PalmResult& palm_result,
                                               const HandResult& hand_result) {
    const RawFrame frame = BuildRawFrame(palm_result, hand_result);
    if (RawFrameHasInput(frame)) {
        consecutive_no_input_frames = 0;
    } else {
        consecutive_no_input_frames += 1;
    }

    if (no_input_reset_frames > 0 && consecutive_no_input_frames >= no_input_reset_frames) {
        ResetSequence();
        return;
    }

    ring_buffer[write_index] = frame;
    write_index = (write_index + 1) % kFullCascadeWindowFrames;
    frames_seen += 1;
}

void FULLCASCADEGESTURERECOGNIZER::ResetSequence() {
    for (size_t i = 0; i < ring_buffer.size(); i++) {
        ring_buffer[i].Clear();
    }
    std::fill(input_buffer.begin(), input_buffer.end(), 0.0f);
    std::fill(input_buffer_int8.begin(), input_buffer_int8.end(), 0);
    std::fill(input_buffer_uint8.begin(), input_buffer_uint8.end(), 0);
    write_index = 0;
    frames_seen = 0;
    last_top_index = -1;
    stable_count = 0;
    consecutive_no_input_frames = 0;
}

FULLCASCADEGESTURERECOGNIZER::RawFrame
FULLCASCADEGESTURERECOGNIZER::BuildRawFrame(const PalmResult& palm_result,
                                            const HandResult& hand_result) const {
    RawFrame frame;
    std::vector<Candidate> candidates;

    for (size_t palm_idx = 0; palm_idx < palm_result.detections.size(); palm_idx++) {
        const HandDetection* paired_hand = nullptr;
        if (palm_idx < hand_result.detections.size()) {
            paired_hand = &hand_result.detections[palm_idx];
        }

        Candidate candidate = BuildCandidate(palm_result.detections[palm_idx], paired_hand);
        if (candidate.valid) {
            candidates.push_back(candidate);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.cx == b.cx) {
            return a.score > b.score;
        }
        return a.cx < b.cx;
    });

    if (candidates.empty()) {
        return frame;
    }

    if (candidates.size() == 1) {
        const int slot = candidates[0].cx < 0.5f ? 0 : 1;
        for (int point = 0; point < kFullCascadePointsPerHand; point++) {
            frame.values[slot][point][0] = candidates[0].values[point][0];
            frame.values[slot][point][1] = candidates[0].values[point][1];
            frame.values[slot][point][2] = candidates[0].values[point][2];
        }
        return frame;
    }

    const size_t count = std::min<size_t>(2, candidates.size());
    for (size_t slot = 0; slot < count; slot++) {
        for (int point = 0; point < kFullCascadePointsPerHand; point++) {
            frame.values[slot][point][0] = candidates[slot].values[point][0];
            frame.values[slot][point][1] = candidates[slot].values[point][1];
            frame.values[slot][point][2] = candidates[slot].values[point][2];
        }
    }
    return frame;
}

FULLCASCADEGESTURERECOGNIZER::Candidate
FULLCASCADEGESTURERECOGNIZER::BuildCandidate(const PalmDetection& palm_detection,
                                             const HandDetection* hand_detection) const {
    Candidate candidate;
    if (!IsPalmUsable(palm_detection)) {
        return candidate;
    }

    const float x1 = std::min(palm_detection.pixel_box[0], palm_detection.pixel_box[2]);
    const float y1 = std::min(palm_detection.pixel_box[1], palm_detection.pixel_box[3]);
    const float x2 = std::max(palm_detection.pixel_box[0], palm_detection.pixel_box[2]);
    const float y2 = std::max(palm_detection.pixel_box[1], palm_detection.pixel_box[3]);

    SetCandidatePoint(&candidate, kFullCascadePalmBoxPointStart + 0, x1, y1);
    SetCandidatePoint(&candidate, kFullCascadePalmBoxPointStart + 1, x2, y1);
    SetCandidatePoint(&candidate, kFullCascadePalmBoxPointStart + 2, x2, y2);
    SetCandidatePoint(&candidate, kFullCascadePalmBoxPointStart + 3, x1, y2);
    SetCandidatePoint(&candidate,
                      kFullCascadePalmP0Index,
                      static_cast<float>(palm_detection.keypoints[0].pixel_x),
                      static_cast<float>(palm_detection.keypoints[0].pixel_y));
    SetCandidatePoint(&candidate,
                      kFullCascadePalmP9Index,
                      static_cast<float>(palm_detection.keypoints[1].pixel_x),
                      static_cast<float>(palm_detection.keypoints[1].pixel_y));

    float cx1 = 0.0f;
    float cy1 = 0.0f;
    float cx2 = 0.0f;
    float cy2 = 0.0f;
    ProjectPixelPointToFeature(x1, y1, &cx1, &cy1);
    ProjectPixelPointToFeature(x2, y2, &cx2, &cy2);
    candidate.cx = Clamp(0.5f * (cx1 + cx2), 0.0f, 1.0f);
    candidate.score = palm_detection.score;

    if (hand_detection != nullptr && IsHandUsable(*hand_detection)) {
        for (int lm = 0; lm < kHandNumLandmarks; lm++) {
            SetCandidatePoint(&candidate,
                              lm,
                              static_cast<float>(hand_detection->landmarks[lm].pixel_x),
                              static_cast<float>(hand_detection->landmarks[lm].pixel_y));
        }
        candidate.score += hand_detection->has_hand_flag ? hand_detection->hand_flag_score : 1.0f;
    }

    candidate.valid = true;
    return candidate;
}

void FULLCASCADEGESTURERECOGNIZER::SetCandidatePoint(Candidate* candidate,
                                                     int point_index,
                                                     float pixel_x,
                                                     float pixel_y) const {
    if (candidate == nullptr || point_index < 0 || point_index >= kFullCascadePointsPerHand) {
        return;
    }

    float x_norm = 0.0f;
    float y_norm = 0.0f;
    ProjectPixelPointToFeature(pixel_x, pixel_y, &x_norm, &y_norm);
    candidate->values[point_index][0] = x_norm;
    candidate->values[point_index][1] = y_norm;
    candidate->values[point_index][2] = 1.0f;
}

void FULLCASCADEGESTURERECOGNIZER::ProjectPixelPointToFeature(float pixel_x,
                                                              float pixel_y,
                                                              float* x_norm,
                                                              float* y_norm) const {
    float x = pixel_x;
    float y = pixel_y;

    if (rotate_features_clockwise) {
        const float rotated_x = static_cast<float>(image_shape[1] - 1) - y;
        const float rotated_y = x;
        x = rotated_x;
        y = rotated_y;
    }

    const float denom_x = static_cast<float>(std::max(1, feature_shape[0] - 1));
    const float denom_y = static_cast<float>(std::max(1, feature_shape[1] - 1));
    *x_norm = Clamp(x / denom_x, 0.0f, 1.0f);
    *y_norm = Clamp(y / denom_y, 0.0f, 1.0f);
}

bool FULLCASCADEGESTURERECOGNIZER::IsPalmUsable(const PalmDetection& detection) const {
    return detection.score >= min_palm_score;
}

bool FULLCASCADEGESTURERECOGNIZER::IsHandUsable(const HandDetection& detection) const {
    if (!detection.valid) {
        return false;
    }
    if (detection.has_hand_flag && detection.hand_flag_score < min_hand_score) {
        return false;
    }
    return true;
}

void FULLCASCADEGESTURERECOGNIZER::BuildModelInput(uint32_t* valid_frames_in_window) {
    std::fill(input_buffer.begin(), input_buffer.end(), 0.0f);
    if (valid_frames_in_window != nullptr) {
        *valid_frames_in_window = 0;
    }

    const uint32_t available = std::min<uint32_t>(frames_seen, kFullCascadeWindowFrames);
    const uint32_t dst_offset = kFullCascadeWindowFrames - available;
    const uint32_t oldest = frames_seen >= kFullCascadeWindowFrames ? write_index : 0;

    for (uint32_t n = 0; n < available; n++) {
        const uint32_t t = dst_offset + n;
        const RawFrame& raw = ring_buffer[(oldest + n) % kFullCascadeWindowFrames];

        bool valid_any = false;
        float sum_x = 0.0f;
        float sum_y = 0.0f;
        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        int valid_count = 0;

        for (int side = 0; side < kGestureNumHands; side++) {
            for (int point = 0; point < kFullCascadePointsPerHand; point++) {
                if (raw.values[side][point][2] <= 0.5f) {
                    continue;
                }
                const float x = raw.values[side][point][0];
                const float y = raw.values[side][point][1];
                sum_x += x;
                sum_y += y;
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
                valid_count += 1;
                valid_any = true;
            }
        }

        if (!valid_any || valid_count <= 0) {
            continue;
        }

        if (valid_frames_in_window != nullptr) {
            *valid_frames_in_window += 1;
        }

        const float center_x = sum_x / static_cast<float>(valid_count);
        const float center_y = sum_y / static_cast<float>(valid_count);
        const float span_x = max_x - min_x;
        const float span_y = max_y - min_y;
        const float scale =
            std::max(kFullCascadeGestureMinNormScale, std::max(span_x, span_y));

        for (int side = 0; side < kGestureNumHands; side++) {
            const int joint_base = side * kFullCascadePointsPerHand;
            for (int point = 0; point < kFullCascadePointsPerHand; point++) {
                const bool valid = raw.values[side][point][2] > 0.5f;
                if (!valid) {
                    continue;
                }
                const int joint = joint_base + point;
                const float norm_x =
                    Clamp((raw.values[side][point][0] - center_x) / scale, -2.0f, 2.0f);
                const float norm_y =
                    Clamp((raw.values[side][point][1] - center_y) / scale, -2.0f, 2.0f);

                input_buffer[((0 * kFullCascadeNumJoints + joint) * kFullCascadeWindowFrames) +
                             t] = norm_x;
                input_buffer[((1 * kFullCascadeNumJoints + joint) * kFullCascadeWindowFrames) +
                             t] = norm_y;
                input_buffer[((2 * kFullCascadeNumJoints + joint) * kFullCascadeWindowFrames) +
                             t] = 1.0f;
                input_buffer[((3 * kFullCascadeNumJoints + joint) * kFullCascadeWindowFrames) +
                             t] = kSideValue[side];
            }
        }
    }
}

bool FULLCASCADEGESTURERECOGNIZER::RunInference(GestureResult* result) {
    if (result == nullptr) {
        return false;
    }
    if (ShouldSkipInferenceAfterFailures()) {
        return false;
    }

    const void* input_data = nullptr;
    int input_bytes = 0;
    if (!PackRuntimeInput(&input_data, &input_bytes)) {
        std::cerr << "[FULLCASCADE] PackRuntimeInput failed for dtype=" << runtime_input_dtype
                  << std::endl;
        return false;
    }

    const int input_ret =
        load_tensor_buffer_ptr(inputs[0], const_cast<void*>(input_data), input_bytes);
    if (input_ret != 0) {
        consecutive_inference_failures += 1;
        PrintInputFailureLog(input_ret, "load_tensor_buffer_ptr");
        return false;
    }

    const int infer_ret = ssne_inference(model_id, 1, inputs);
    if (infer_ret != 0) {
        consecutive_inference_failures += 1;
        PrintInputFailureLog(infer_ret, "ssne_inference");
        return false;
    }
    consecutive_inference_failures = 0;

    const int output_ret = ssne_getoutput(model_id, 1, outputs);
    if (output_ret != 0) {
        std::cerr << "[FULLCASCADE] ssne_getoutput failed, ret=" << output_ret << std::endl;
        return false;
    }

    const TensorDebugInfo output_info = GetTensorDebugInfo(outputs[0]);
    if (!output_info_logged) {
        std::cout << "[FULLCASCADE] Output tensor: width=" << output_info.width
                  << ", height=" << output_info.height
                  << ", dtype=" << static_cast<int>(output_info.dtype)
                  << ", format=" << static_cast<int>(output_info.format)
                  << ", mem_size=" << output_info.mem_size
                  << ", total_size=" << output_info.total_size
                  << ", inferred_elements=" << output_info.inferred_elements
                  << std::endl;
        output_info_logged = true;
    }

    if (output_info.inferred_elements < static_cast<size_t>(kGestureNumClasses)) {
        std::cerr << "[FULLCASCADE] Output tensor too small: elements="
                  << output_info.inferred_elements << std::endl;
        return false;
    }

    float max_logit = -std::numeric_limits<float>::infinity();
    int top_index = -1;
    for (int i = 0; i < kGestureNumClasses; i++) {
        result->logits[i] = ReadTensorValue(outputs[0], output_info, static_cast<size_t>(i));
        if (result->logits[i] > max_logit) {
            max_logit = result->logits[i];
            top_index = i;
        }
    }

    float prob_sum = 0.0f;
    for (int i = 0; i < kGestureNumClasses; i++) {
        result->probabilities[i] = std::exp(result->logits[i] - max_logit);
        prob_sum += result->probabilities[i];
    }
    if (prob_sum > 0.0f) {
        for (int i = 0; i < kGestureNumClasses; i++) {
            result->probabilities[i] /= prob_sum;
        }
    }

    result->top_index = top_index;
    result->valid = top_index >= 0;
    return result->valid;
}

bool FULLCASCADEGESTURERECOGNIZER::ShouldSkipInferenceAfterFailures() const {
    return consecutive_inference_failures >= 5;
}

bool FULLCASCADEGESTURERECOGNIZER::PackRuntimeInput(const void** data, int* bytes) {
    if (data == nullptr || bytes == nullptr) {
        return false;
    }

    if (runtime_input_dtype == SSNE_FLOAT32) {
        *data = input_buffer.data();
        *bytes = static_cast<int>(input_buffer.size() * sizeof(float));
        return true;
    }

    if (runtime_input_dtype == SSNE_INT8) {
        for (size_t i = 0; i < input_buffer.size(); i++) {
            const float q = std::round(input_buffer[i] * input_quant_scale);
            input_buffer_int8[i] = static_cast<int8_t>(Clamp(q, -128.0f, 127.0f));
        }
        *data = input_buffer_int8.data();
        *bytes = static_cast<int>(input_buffer_int8.size() * sizeof(int8_t));
        return true;
    }

    if (runtime_input_dtype == SSNE_UINT8) {
        for (size_t i = 0; i < input_buffer.size(); i++) {
            const float q = std::round(input_buffer[i] * input_quant_scale + 128.0f);
            input_buffer_uint8[i] = static_cast<uint8_t>(Clamp(q, 0.0f, 255.0f));
        }
        *data = input_buffer_uint8.data();
        *bytes = static_cast<int>(input_buffer_uint8.size() * sizeof(uint8_t));
        return true;
    }

    return false;
}

size_t FULLCASCADEGESTURERECOGNIZER::RuntimeInputBytes() const {
    return static_cast<size_t>(kFullCascadeInputElements) * DTypeElementBytes(runtime_input_dtype);
}

void FULLCASCADEGESTURERECOGNIZER::PrintInitializeLog(const std::string& model_path) const {
    const int input_num = ssne_get_model_input_num(model_id);

    int mean[3] = {0, 0, 0};
    int std_scale[3] = {0, 0, 0};
    int is_uint8 = 0;
    const int norm_ret = ssne_get_model_normalize_params(model_id, mean, std_scale, &is_uint8);
    const TensorDebugInfo input_info = GetTensorDebugInfo(inputs[0]);

    std::cout << "[FULLCASCADE] Loaded model: " << model_path
              << ", model_id=" << model_id << std::endl;
    std::cout << "[FULLCASCADE] Model input_num=" << input_num
              << ", model_input_dtype=" << model_input_dtype
              << ", runtime_input_dtype=" << runtime_input_dtype
              << ", normalize_ret=" << norm_ret
              << ", mean=(" << mean[0] << "," << mean[1] << "," << mean[2] << ")"
              << ", std=(" << std_scale[0] << "," << std_scale[1] << "," << std_scale[2] << ")"
              << ", is_uint8=" << is_uint8 << std::endl;
    std::cout << "[FULLCASCADE] Input contract: INPUT0 float32 shape=1x4x54x64, bytes="
              << kFullCascadeInputBytes << std::endl;
    std::cout << "[FULLCASCADE] Runtime input tensor: width=" << input_info.width
              << ", height=" << input_info.height
              << ", dtype=" << static_cast<int>(input_info.dtype)
              << ", format=" << static_cast<int>(input_info.format)
              << ", mem_size=" << input_info.mem_size
              << ", total_size=" << input_info.total_size
              << ", inferred_elements=" << input_info.inferred_elements
              << std::endl;
    if (input_info.mem_size != RuntimeInputBytes()) {
        std::cerr << "[FULLCASCADE] Warning: runtime tensor mem_size=" << input_info.mem_size
                  << " differs from dtype-adjusted expected_bytes=" << RuntimeInputBytes()
                  << std::endl;
    }
    std::cout << "[FULLCASCADE] Feature config: raw_image=" << image_shape[0] << "x"
              << image_shape[1]
              << ", feature_image=" << feature_shape[0] << "x" << feature_shape[1]
              << ", rotate_features_clockwise=" << (rotate_features_clockwise ? 1 : 0)
              << ", tensor_mode="
              << (input_tensor_mode == kGestureInputTensorModeFlatBytes ? "flat_bytes" : "nchw")
              << ", min_valid_frames=" << min_valid_frames
              << ", input_quant_scale=" << input_quant_scale
              << ", min_palm_score=" << min_palm_score
              << ", min_hand_score=" << min_hand_score
              << ", warmup_frames=" << warmup_frames
              << ", debug_interval=" << debug_interval
              << ", stable_hits=" << stable_hits
              << ", verbose_log=" << (verbose_log ? 1 : 0)
              << ", no_input_reset_frames=" << no_input_reset_frames
              << std::endl;
    std::cout << "[FULLCASCADE] Point layout per side: 0..20=hand, 21..24=palm_box, "
                 "25=palm_p0, 26=palm_p9"
              << std::endl;
    std::cout << "[FULLCASCADE] Classes: 0=rain, 1=long, 2=short, 3=go, 4=thick"
              << std::endl;
}

void FULLCASCADEGESTURERECOGNIZER::PrintInputFailureLog(int ret, const char* stage) const {
    const TensorDebugInfo input_info = GetTensorDebugInfo(inputs[0]);
    std::cerr << "[FULLCASCADE] " << stage << " failed, ret=" << ret
              << ", consecutive_failures=" << consecutive_inference_failures
              << ", model_input_dtype=" << model_input_dtype
              << ", tensor_mode="
              << (input_tensor_mode == kGestureInputTensorModeFlatBytes ? "flat_bytes" : "nchw")
              << ", input_width=" << input_info.width
              << ", input_height=" << input_info.height
              << ", input_dtype=" << static_cast<int>(input_info.dtype)
              << ", input_format=" << static_cast<int>(input_info.format)
              << ", input_mem_size=" << input_info.mem_size
              << ", input_total_size=" << input_info.total_size
              << ", input_inferred_elements=" << input_info.inferred_elements
              << ", expected_float_bytes=" << kFullCascadeInputBytes
              << ", expected_runtime_bytes=" << RuntimeInputBytes()
              << std::endl;
    if (ret == SSNE_ERRCODE_INPUT_DTYPE_ERROR) {
        std::cerr << "[FULLCASCADE] ret=403 means INPUT_DTYPE_ERROR. Check the converted "
                     "SSTCN input dtype and --fullcascade_tensor_mode."
                  << std::endl;
    }
    if (consecutive_inference_failures == 5) {
        std::cerr << "[FULLCASCADE] Stop trying SSTCN inference after 5 consecutive failures."
                  << std::endl;
    }
}

void FULLCASCADEGESTURERECOGNIZER::PrintPredictLog(uint32_t frame_index,
                                                   const GestureResult& result,
                                                   const FeatureStats& stats,
                                                   const PalmResult& palm_result,
                                                   const HandResult& hand_result) const {
    const float top_prob =
        result.top_index >= 0 ? result.probabilities[result.top_index] : 0.0f;
    if (!verbose_log) {
        std::cout << "[FULLCASCADE]"
                  << " f=" << frame_index
                  << " pred=" << result.top_index << "(" << ClassName(result.top_index) << ")"
                  << " p=" << std::fixed << std::setprecision(3) << top_prob
                  << " stable=" << result.stable_index << "(" << ClassName(result.stable_index)
                  << ")"
                  << " sc=" << result.stable_count
                  << " valid=" << result.valid_frames_in_window
                  << " palm=" << palm_result.detections.size()
                  << " hand=" << hand_result.detections.size()
                  << std::endl;
        return;
    }

    std::cout << "[FULLCASCADE][frame " << frame_index << "]"
              << " pred=" << result.top_index << "(" << ClassName(result.top_index) << ")"
              << ", prob=" << std::fixed << std::setprecision(3) << top_prob
              << ", stable=" << result.stable_index << "(" << ClassName(result.stable_index)
              << ")"
              << ", stable_count=" << result.stable_count
              << ", buffered=" << result.buffered_frames
              << ", valid_frames=" << result.valid_frames_in_window
              << ", palm_dets=" << palm_result.detections.size()
              << ", hand_dets=" << hand_result.detections.size()
              << ", logits=" << ArrayToString(result.logits)
              << ", probs=" << ArrayToString(result.probabilities)
              << ", input_min=" << stats.min_value
              << ", input_max=" << stats.max_value
              << ", input_mean=" << stats.mean_value
              << ", input_nonzero=" << stats.nonzero_count << "/" << input_buffer.size()
              << std::endl;

    for (size_t i = 0; i < palm_result.detections.size(); i++) {
        const PalmDetection& det = palm_result.detections[i];
        std::cout << "[FULLCASCADE][frame " << frame_index << "] palm[" << i
                  << "] score=" << det.score
                  << ", box=(" << det.pixel_box[0] << "," << det.pixel_box[1] << ","
                  << det.pixel_box[2] << "," << det.pixel_box[3] << ")"
                  << ", p0=(" << det.keypoints[0].pixel_x << "," << det.keypoints[0].pixel_y
                  << ")"
                  << ", p9=(" << det.keypoints[1].pixel_x << "," << det.keypoints[1].pixel_y
                  << ")"
                  << std::endl;
    }

    for (size_t i = 0; i < hand_result.detections.size(); i++) {
        const HandDetection& det = hand_result.detections[i];
        std::cout << "[FULLCASCADE][frame " << frame_index << "] hand[" << i
                  << "] valid=" << (det.valid ? 1 : 0)
                  << ", flag=" << (det.has_hand_flag ? det.hand_flag_score : -1.0f)
                  << ", handedness=" << (det.has_handedness ? det.handedness_score : -1.0f)
                  << ", wrist=(" << det.landmarks[0].pixel_x << "," << det.landmarks[0].pixel_y
                  << ")"
                  << ", middle_mcp=(" << det.landmarks[9].pixel_x << ","
                  << det.landmarks[9].pixel_y << ")"
                  << std::endl;
    }
}
