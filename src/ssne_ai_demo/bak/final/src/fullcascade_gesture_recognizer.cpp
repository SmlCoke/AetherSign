#include "../include/common.hpp"
#include "../include/fullcascade_golden_samples.hpp"

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
    float in_output_quant_scale,
    float in_min_palm_score,
    uint32_t in_warmup_frames,
    uint32_t in_debug_interval,
    uint32_t in_stable_hits,
    bool in_verbose_log,
    uint32_t in_no_input_reset_frames,
    bool in_diagnostic_repeat) {
    image_shape = in_image_shape;
    rotate_features_clockwise = in_rotate_features_clockwise;
    input_tensor_mode = in_input_tensor_mode;
    min_valid_frames = std::min<uint32_t>(in_min_valid_frames, kFullCascadeWindowFrames);
    input_quant_scale = in_input_quant_scale > 0.0f ? in_input_quant_scale : 64.0f;
    output_quant_scale =
        in_output_quant_scale > 0.0f ? in_output_quant_scale : kFullCascadeDefaultOutputQuantScale;
    min_palm_score = in_min_palm_score;
    warmup_frames =
        std::max<uint32_t>(1, std::min<uint32_t>(in_warmup_frames, kFullCascadeWindowFrames));
    debug_interval = std::max<uint32_t>(1, in_debug_interval);
    stable_hits = std::max<uint32_t>(1, in_stable_hits);
    verbose_log = in_verbose_log;
    no_input_reset_frames = in_no_input_reset_frames;
    diagnostic_repeat = in_diagnostic_repeat;

    feature_shape = rotate_features_clockwise
                        ? std::array<int, 2>{{image_shape[1], image_shape[0]}}
                        : image_shape;

    ring_buffer.assign(kFullCascadeWindowFrames, RawFrame());
    input_buffer.assign(kFullCascadeInputElements, 0.0f);
    input_buffer_int8.assign(kFullCascadeInputElements, 0);
    input_buffer_uint8.assign(kFullCascadeInputElements, 0);
    pre_roll_buffer.clear();
    frozen_result.Clear();
    capture_state = kCaptureWaiting;
    write_index = 0;
    frames_seen = 0;
    predict_count = 0;
    consecutive_no_input_frames = 0;
    trigger_hits = 0;
    frozen_no_input_frames = 0;
    has_frozen_result = false;
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

    const RawFrame current_frame = BuildRawFrame(palm_result, hand_result);
    const bool has_input = RawFrameHasInput(current_frame);

    if (capture_state == kCaptureFrozen) {
        if (has_input) {
            frozen_no_input_frames = 0;
        } else {
            frozen_no_input_frames += 1;
        }

        if (frozen_no_input_frames >= kFullCascadeFreezeReleaseNoInputFrames) {
            capture_state = kCaptureWaiting;
            pre_roll_buffer.clear();
            trigger_hits = 0;
            has_frozen_result = false;
            SetNoGestureResult(result);
            if (frame_index % debug_interval == 0 || verbose_log) {
                std::cout << "[FULLCASCADE][frame " << frame_index
                          << "] release_frozen_result -> no_gesture"
                          << ", no_input_samples=" << frozen_no_input_frames << std::endl;
            }
            frozen_no_input_frames = 0;
            return;
        }

        CopyFrozenResult(result);
        if (frame_index % debug_interval == 0 && has_frozen_result) {
            std::cout << "[FULLCASCADE][frame " << frame_index << "] frozen_result"
                      << " pred=" << result->stable_index << "("
                      << ClassName(result->stable_index) << ")"
                      << ", no_input_samples=" << frozen_no_input_frames
                      << ", palm=" << palm_result.detections.size()
                      << ", hand=" << hand_result.detections.size() << std::endl;
        }
        return;
    }

    if (capture_state == kCaptureWaiting) {
        StorePreRollFrame(current_frame);
        trigger_hits = has_input ? trigger_hits + 1 : 0;

        if (trigger_hits < kFullCascadeTriggerHits) {
            if (has_input) {
                result->buffered_frames = static_cast<uint32_t>(pre_roll_buffer.size());
                result->ready = false;
            } else {
                // Training labels an entirely hand-free temporal window as
                // no_gesture. While waiting, the equivalent all-zero window
                // is deterministic, so return class 5 without spending NPU
                // time. Once capture starts, zero frames are still appended
                // at their original time positions by AppendRawFrame().
                SetNoGestureResult(result);
            }
            if (frame_index % debug_interval == 0 || (has_input && verbose_log)) {
                std::cout << "[FULLCASCADE][frame " << frame_index << "] wait_trigger"
                          << " hits=" << trigger_hits << "/" << kFullCascadeTriggerHits
                          << ", pre_roll=" << pre_roll_buffer.size()
                          << ", palm=" << palm_result.detections.size()
                          << ", hand=" << hand_result.detections.size() << std::endl;
            }
            return;
        }

        StartCaptureFromPreRoll();
        capture_state = kCaptureCollecting;
        if (verbose_log || frame_index % debug_interval == 0) {
            std::cout << "[FULLCASCADE][frame " << frame_index << "] trigger_capture"
                      << " pre_roll=" << frames_seen
                      << ", palm=" << palm_result.detections.size()
                      << ", hand=" << hand_result.detections.size() << std::endl;
        }
    } else {
        if (!AppendRawFrame(current_frame)) {
            SetNoGestureResult(result);
            if (frame_index % debug_interval == 0 || verbose_log) {
                std::cout << "[FULLCASCADE][frame " << frame_index
                          << "] no_input_reset -> no_gesture"
                          << ", no_input_samples=" << no_input_reset_frames
                          << std::endl;
            }
            return;
        }
    }

    result->buffered_frames = std::min<uint32_t>(frames_seen, kFullCascadeWindowFrames);
    result->ready = result->buffered_frames >= warmup_frames;
    if (!result->ready) {
        if (frame_index % debug_interval == 0) {
            std::cout << "[FULLCASCADE][frame " << frame_index << "] capture_window buffered="
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
    const bool diagnostic_log_due = verbose_log || frame_index % debug_interval == 0;
    FeatureStats stats;
    if (diagnostic_log_due) {
        stats = GetFeatureStats(input_buffer);
    }
    const uint32_t required_valid_frames =
        std::max(min_valid_frames, kFullCascadeMinTriggeredValidFrames);
    if (valid_frames_in_window < required_valid_frames) {
        ResetSequence();
        SetNoGestureResult(result);
        if (frame_index % debug_interval == 0 || verbose_log) {
            std::cout << "[FULLCASCADE][frame " << frame_index
                      << "] skip_empty_window -> no_gesture valid_frames=" << valid_frames_in_window
                      << "/" << required_valid_frames
                      << ", palm=" << palm_result.detections.size()
                      << ", hand=" << hand_result.detections.size()
                      << ", input_nonzero=" << stats.nonzero_count << "/" << input_buffer.size()
                      << std::endl;
        }
        return;
    }

    if (diagnostic_log_due) {
        const WindowDiagnostics window_diag = GetWindowDiagnostics();
        PrintWindowDiagnostics(frame_index, window_diag);
        const ModelInputDiagnostics model_input_diag = GetModelInputDiagnostics();
        PrintModelInputDiagnostics(frame_index, model_input_diag);
    }

    if (!RunInference(result)) {
        return;
    }

    const int raw_classifier_top = result->top_index;
    const float raw_classifier_prob =
        raw_classifier_top >= 0 ? result->probabilities[raw_classifier_top] : 0.0f;
    if (diagnostic_log_due) {
        std::cout << "[FULLCASCADE][RAW] f=" << frame_index
                  << " pred=" << raw_classifier_top << "(" << ClassName(raw_classifier_top) << ")"
                  << " p=" << std::fixed << std::setprecision(3) << raw_classifier_prob
                  << " logits=" << ArrayToString(result->logits)
                  << " probs=" << ArrayToString(result->probabilities)
                  << std::endl;
    }

    float reject_margin = 0.0f;
    if (ShouldRejectAsNoGesture(*result, &reject_margin)) {
        const int raw_top_index = result->top_index;
        const float raw_top_prob = raw_top_index >= 0 ? result->probabilities[raw_top_index] : 0.0f;
        SetNoGestureResult(result);
        result->buffered_frames = kFullCascadeWindowFrames;
        result->valid_frames_in_window = valid_frames_in_window;
        if (frame_index % debug_interval == 0 || verbose_log) {
            std::cout << "[FULLCASCADE][frame " << frame_index
                      << "] reject_unknown -> no_gesture"
                      << ", raw_pred=" << raw_top_index << "(" << ClassName(raw_top_index) << ")"
                      << ", raw_prob=" << std::fixed << std::setprecision(3) << raw_top_prob
                      << ", margin=" << reject_margin
                      << std::endl;
        }
    }

    predict_count += 1;
    result->predict_count = predict_count;
    last_top_index = result->top_index;
    stable_count = stable_hits;
    result->stable_count = stable_count;
    result->stable_index = last_top_index;

    if (diagnostic_log_due) {
        PrintPredictLog(frame_index, *result, stats, palm_result, hand_result);
    }
    if (diagnostic_repeat) {
        if (verbose_log) {
            std::cout << "[FULLCASCADE][DIAG] repeat mode: reset capture after prediction"
                      << std::endl;
        }
        ResetSequence();
    } else {
        frozen_result = *result;
        has_frozen_result = true;
        frozen_no_input_frames = 0;
        capture_state = kCaptureFrozen;
    }
}


bool FULLCASCADEGESTURERECOGNIZER::RunGoldenSelfTest() {
    std::cout << "[FULLCASCADE][GOLDEN] begin samples=" << kFullCascadeGoldenSampleCount
              << ", runtime_input_dtype=" << runtime_input_dtype
              << ", configured_quant_scale=" << input_quant_scale << std::endl;

    if (kFullCascadeGoldenSampleCount <= 0 || kFullCascadeGoldenSamples == nullptr) {
        std::cout << "[FULLCASCADE][GOLDEN] SKIP: generated golden header not installed." << std::endl;
        return false;
    }
    if (kFullCascadeGoldenInputElements != input_buffer.size()) {
        std::cerr << "[FULLCASCADE][GOLDEN] FAIL: element mismatch header="
                  << kFullCascadeGoldenInputElements << " runtime=" << input_buffer.size() << std::endl;
        return false;
    }

    // First run the original canonical path. This intentionally reconstructs float input
    // and lets PackRuntimeInput() quantize it again, matching the live inference path.
    int passed = 0;
    for (int sample_index = 0; sample_index < kFullCascadeGoldenSampleCount; sample_index++) {
        const FullCascadeGoldenSample& sample = kFullCascadeGoldenSamples[sample_index];
        for (std::size_t i = 0; i < input_buffer.size(); i++) {
            input_buffer[i] =
                static_cast<float>(sample.data[i]) / kFullCascadeGoldenQuantScale;
        }

        GestureResult result;
        result.Clear();
        const bool ok = RunInference(&result);
        const bool match = ok && result.top_index == sample.expected_label;
        if (match) {
            passed += 1;
        }

        std::cout << "[FULLCASCADE][GOLDEN] canonical sample=" << sample_index
                  << " name=" << sample.name
                  << " expected=" << sample.expected_label << "(" << ClassName(sample.expected_label) << ")"
                  << " pred=" << result.top_index << "(" << ClassName(result.top_index) << ")"
                  << " p=" << std::fixed << std::setprecision(3)
                  << ((result.top_index >= 0) ? result.probabilities[result.top_index] : 0.0f)
                  << " logits=" << ArrayToString(result.logits)
                  << " " << (match ? "PASS" : "FAIL")
                  << std::endl;
    }

    std::cout << "[FULLCASCADE][GOLDEN] canonical_summary=" << passed << "/"
              << kFullCascadeGoldenSampleCount
              << (passed == kFullCascadeGoldenSampleCount ? " PASS" : " FAIL")
              << std::endl;

    if (runtime_input_dtype != SSNE_INT8) {
        std::cout << "[FULLCASCADE][SWEEP] SKIP: layout sweep currently requires INT8 runtime input; dtype="
                  << runtime_input_dtype << std::endl;
        ResetSequence();
        return passed == kFullCascadeGoldenSampleCount;
    }

    // The generated header stores exact canonical NCHW int8 bytes. Feed those bytes
    // directly to SSNE under every permutation of C/J/T linearization. This bypasses
    // float dequant/requant and isolates the runtime's expected linear memory order.
    static const char* kLayoutNames[6] = {
        "C-J-T", "C-T-J", "J-C-T", "J-T-C", "T-C-J", "T-J-C"
    };

    std::vector<int8_t> reordered(kFullCascadeGoldenInputElements, 0);
    int best_passed = -1;
    int best_layout = -1;

    for (int layout_index = 0; layout_index < 6; layout_index++) {
        int layout_passed = 0;
        std::cout << "[FULLCASCADE][SWEEP] layout=" << kLayoutNames[layout_index]
                  << " begin" << std::endl;

        for (int sample_index = 0; sample_index < kFullCascadeGoldenSampleCount; sample_index++) {
            const FullCascadeGoldenSample& sample = kFullCascadeGoldenSamples[sample_index];
            BuildGoldenLayoutInt8(sample.data, layout_index, &reordered);

            GestureResult result;
            result.Clear();
            const bool ok = RunInferencePacked(reordered.data(),
                                               static_cast<int>(reordered.size()),
                                               &result);
            const bool match = ok && result.top_index == sample.expected_label;
            if (match) {
                layout_passed += 1;
            }

            std::cout << "[FULLCASCADE][SWEEP] layout=" << kLayoutNames[layout_index]
                      << " sample=" << sample_index
                      << " name=" << sample.name
                      << " expected=" << sample.expected_label << "(" << ClassName(sample.expected_label) << ")"
                      << " pred=" << result.top_index << "(" << ClassName(result.top_index) << ")"
                      << " p=" << std::fixed << std::setprecision(3)
                      << ((result.top_index >= 0) ? result.probabilities[result.top_index] : 0.0f)
                      << " logits=" << ArrayToString(result.logits)
                      << " " << (match ? "PASS" : "FAIL")
                      << std::endl;
        }

        std::cout << "[FULLCASCADE][SWEEP] layout=" << kLayoutNames[layout_index]
                  << " summary=" << layout_passed << "/" << kFullCascadeGoldenSampleCount
                  << (layout_passed == kFullCascadeGoldenSampleCount ? " PASS" : " FAIL")
                  << std::endl;

        if (layout_passed > best_passed) {
            best_passed = layout_passed;
            best_layout = layout_index;
        }
    }

    std::cout << "[FULLCASCADE][SWEEP] best_layout="
              << (best_layout >= 0 ? kLayoutNames[best_layout] : "none")
              << " best_summary=" << best_passed << "/" << kFullCascadeGoldenSampleCount
              << std::endl;

    if (best_passed == kFullCascadeGoldenSampleCount) {
        std::cout << "[FULLCASCADE][SWEEP] FOUND_3_OF_3_LAYOUT=" << kLayoutNames[best_layout]
                  << std::endl;
    } else {
        std::cout << "[FULLCASCADE][SWEEP] NO_LAYOUT_RESTORED_3_OF_3" << std::endl;
    }

    ResetSequence();
    return best_passed == kFullCascadeGoldenSampleCount;
}

size_t FULLCASCADEGESTURERECOGNIZER::GoldenCanonicalIndex(int c, int j, int t) {
    return (static_cast<size_t>(c) * kFullCascadeNumJoints + static_cast<size_t>(j)) *
               kFullCascadeWindowFrames +
           static_cast<size_t>(t);
}

void FULLCASCADEGESTURERECOGNIZER::BuildGoldenLayoutInt8(
    const int8_t* canonical,
    int layout_index,
    std::vector<int8_t>* reordered) {
    if (canonical == nullptr || reordered == nullptr) {
        return;
    }
    reordered->assign(kFullCascadeInputElements, 0);
    size_t dst = 0;

    // canonical source is x[c][j][t]. Each case changes which axis is outer/middle/inner.
    switch (layout_index) {
        case 0:  // C-J-T
            for (int c = 0; c < kFullCascadeInputChannels; c++)
                for (int j = 0; j < kFullCascadeNumJoints; j++)
                    for (int t = 0; t < kFullCascadeWindowFrames; t++)
                        (*reordered)[dst++] = canonical[GoldenCanonicalIndex(c, j, t)];
            break;
        case 1:  // C-T-J
            for (int c = 0; c < kFullCascadeInputChannels; c++)
                for (int t = 0; t < kFullCascadeWindowFrames; t++)
                    for (int j = 0; j < kFullCascadeNumJoints; j++)
                        (*reordered)[dst++] = canonical[GoldenCanonicalIndex(c, j, t)];
            break;
        case 2:  // J-C-T
            for (int j = 0; j < kFullCascadeNumJoints; j++)
                for (int c = 0; c < kFullCascadeInputChannels; c++)
                    for (int t = 0; t < kFullCascadeWindowFrames; t++)
                        (*reordered)[dst++] = canonical[GoldenCanonicalIndex(c, j, t)];
            break;
        case 3:  // J-T-C
            for (int j = 0; j < kFullCascadeNumJoints; j++)
                for (int t = 0; t < kFullCascadeWindowFrames; t++)
                    for (int c = 0; c < kFullCascadeInputChannels; c++)
                        (*reordered)[dst++] = canonical[GoldenCanonicalIndex(c, j, t)];
            break;
        case 4:  // T-C-J
            for (int t = 0; t < kFullCascadeWindowFrames; t++)
                for (int c = 0; c < kFullCascadeInputChannels; c++)
                    for (int j = 0; j < kFullCascadeNumJoints; j++)
                        (*reordered)[dst++] = canonical[GoldenCanonicalIndex(c, j, t)];
            break;
        case 5:  // T-J-C
            for (int t = 0; t < kFullCascadeWindowFrames; t++)
                for (int j = 0; j < kFullCascadeNumJoints; j++)
                    for (int c = 0; c < kFullCascadeInputChannels; c++)
                        (*reordered)[dst++] = canonical[GoldenCanonicalIndex(c, j, t)];
            break;
        default:
            for (size_t i = 0; i < kFullCascadeInputElements; i++) {
                (*reordered)[i] = canonical[i];
            }
            break;
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
        case 5:
            return "no_gesture";
        default:
            return "unknown";
    }
}

const char* FULLCASCADEGESTURERECOGNIZER::CaptureStateName(CaptureState state) {
    switch (state) {
        case kCaptureWaiting:
            return "waiting";
        case kCaptureCollecting:
            return "collecting";
        case kCaptureFrozen:
            return "frozen";
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

bool FULLCASCADEGESTURERECOGNIZER::RawFrameSlotHasPalm(const RawFrame& frame, int side) {
    if (side < 0 || side >= kGestureNumHands) {
        return false;
    }
    for (int point = kFullCascadePalmBoxPointStart; point < kFullCascadePointsPerHand; point++) {
        if (frame.values[side][point][2] > 0.5f) {
            return true;
        }
    }
    return false;
}

bool FULLCASCADEGESTURERECOGNIZER::RawFrameSlotHasHand(const RawFrame& frame, int side) {
    if (side < 0 || side >= kGestureNumHands) {
        return false;
    }
    for (int point = 0; point < kHandNumLandmarks; point++) {
        if (frame.values[side][point][2] > 0.5f) {
            return true;
        }
    }
    return false;
}

void FULLCASCADEGESTURERECOGNIZER::StorePreRollFrame(const RawFrame& frame) {
    pre_roll_buffer.push_back(frame);
    while (pre_roll_buffer.size() > kFullCascadePreRollFrames) {
        pre_roll_buffer.erase(pre_roll_buffer.begin());
    }
}

void FULLCASCADEGESTURERECOGNIZER::StartCaptureFromPreRoll() {
    for (size_t i = 0; i < ring_buffer.size(); i++) {
        ring_buffer[i].Clear();
    }
    write_index = 0;
    frames_seen = 0;
    consecutive_no_input_frames = 0;

    for (size_t i = 0; i < pre_roll_buffer.size(); i++) {
        AppendRawFrame(pre_roll_buffer[i]);
    }
    pre_roll_buffer.clear();
}

bool FULLCASCADEGESTURERECOGNIZER::AppendRawFrame(const RawFrame& frame) {
    if (RawFrameHasInput(frame)) {
        consecutive_no_input_frames = 0;
    } else {
        consecutive_no_input_frames += 1;
    }

    if (no_input_reset_frames > 0 && consecutive_no_input_frames >= no_input_reset_frames) {
        ResetSequence();
        return false;
    }

    ring_buffer[write_index] = frame;
    write_index = (write_index + 1) % kFullCascadeWindowFrames;
    frames_seen += 1;
    return true;
}

void FULLCASCADEGESTURERECOGNIZER::CopyFrozenResult(GestureResult* result) const {
    if (result == nullptr) {
        return;
    }
    if (has_frozen_result) {
        *result = frozen_result;
    }
}

void FULLCASCADEGESTURERECOGNIZER::SetNoGestureResult(GestureResult* result) const {
    if (result == nullptr) {
        return;
    }
    result->Clear();
    result->top_index = 5;
    result->stable_index = 5;
    result->stable_count = stable_hits;
    result->buffered_frames = std::min<uint32_t>(frames_seen, kFullCascadeWindowFrames);
    result->valid_frames_in_window = 0;
    result->predict_count = predict_count;
    result->ready = true;
    result->valid = true;
    result->probabilities.fill(0.0f);
    result->probabilities[5] = 1.0f;
}

bool FULLCASCADEGESTURERECOGNIZER::ShouldRejectAsNoGesture(const GestureResult& result,
                                                           float* margin) const {
    if (!result.valid || result.top_index < 0 || result.top_index == 5) {
        if (margin != nullptr) {
            *margin = 1.0f;
        }
        return false;
    }

    float top_prob = result.probabilities[result.top_index];
    float second_prob = 0.0f;
    for (int i = 0; i < kGestureNumClasses; i++) {
        if (i == result.top_index) {
            continue;
        }
        second_prob = std::max(second_prob, result.probabilities[i]);
    }

    const float prob_margin = top_prob - second_prob;
    if (margin != nullptr) {
        *margin = prob_margin;
    }
    return top_prob < kFullCascadeRejectMinTopProb ||
           prob_margin < kFullCascadeRejectMinMargin;
}

void FULLCASCADEGESTURERECOGNIZER::ResetSequence() {
    for (size_t i = 0; i < ring_buffer.size(); i++) {
        ring_buffer[i].Clear();
    }
    std::fill(input_buffer.begin(), input_buffer.end(), 0.0f);
    std::fill(input_buffer_int8.begin(), input_buffer_int8.end(), 0);
    std::fill(input_buffer_uint8.begin(), input_buffer_uint8.end(), 0);
    pre_roll_buffer.clear();
    frozen_result.Clear();
    capture_state = kCaptureWaiting;
    write_index = 0;
    frames_seen = 0;
    last_top_index = -1;
    stable_count = 0;
    consecutive_no_input_frames = 0;
    trigger_hits = 0;
    frozen_no_input_frames = 0;
    has_frozen_result = false;
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

    const float x1 = std::min(palm_detection.model_box[0], palm_detection.model_box[2]);
    const float y1 = std::min(palm_detection.model_box[1], palm_detection.model_box[3]);
    const float x2 = std::max(palm_detection.model_box[0], palm_detection.model_box[2]);
    const float y2 = std::max(palm_detection.model_box[1], palm_detection.model_box[3]);

    SetCandidateFeaturePoint(&candidate, kFullCascadePalmBoxPointStart + 0, x1, y1);
    SetCandidateFeaturePoint(&candidate, kFullCascadePalmBoxPointStart + 1, x2, y1);
    SetCandidateFeaturePoint(&candidate, kFullCascadePalmBoxPointStart + 2, x2, y2);
    SetCandidateFeaturePoint(&candidate, kFullCascadePalmBoxPointStart + 3, x1, y2);
    SetCandidateFeaturePoint(&candidate,
                             kFullCascadePalmP0Index,
                             palm_detection.keypoints[0].model_x,
                             palm_detection.keypoints[0].model_y);
    SetCandidateFeaturePoint(&candidate,
                             kFullCascadePalmP9Index,
                             palm_detection.keypoints[1].model_x,
                             palm_detection.keypoints[1].model_y);

    candidate.cx = Clamp(0.5f * (x1 + x2), 0.0f, 1.0f);
    candidate.score = palm_detection.score;

    if (hand_detection != nullptr && IsHandUsable(*hand_detection)) {
        for (int lm = 0; lm < kHandNumLandmarks; lm++) {
            SetCandidatePoint(&candidate,
                              lm,
                              static_cast<float>(hand_detection->landmarks[lm].pixel_x),
                              static_cast<float>(hand_detection->landmarks[lm].pixel_y));
        }
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

void FULLCASCADEGESTURERECOGNIZER::SetCandidateFeaturePoint(Candidate* candidate,
                                                            int point_index,
                                                            float x_norm,
                                                            float y_norm) const {
    if (candidate == nullptr || point_index < 0 || point_index >= kFullCascadePointsPerHand) {
        return;
    }

    candidate->values[point_index][0] = Clamp(x_norm, 0.0f, 1.0f);
    candidate->values[point_index][1] = Clamp(y_norm, 0.0f, 1.0f);
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
    // Hand confidence is intentionally not a feature gate. Training includes
    // missing/low-confidence hand frames, and OSD filtering must never alter
    // the tensor that is sent to the downstream gesture classifier.
    return detection.valid;
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

FULLCASCADEGESTURERECOGNIZER::WindowDiagnostics
FULLCASCADEGESTURERECOGNIZER::GetWindowDiagnostics() const {
    WindowDiagnostics diag;
    diag.frames_available = std::min<uint32_t>(frames_seen, kFullCascadeWindowFrames);
    const uint32_t oldest = frames_seen >= kFullCascadeWindowFrames ? write_index : 0;
    uint32_t current_missing_run[kGestureNumHands] = {0, 0};

    for (uint32_t n = 0; n < diag.frames_available; n++) {
        const RawFrame& raw = ring_buffer[(oldest + n) % kFullCascadeWindowFrames];
        const bool palm0 = RawFrameSlotHasPalm(raw, 0);
        const bool palm1 = RawFrameSlotHasPalm(raw, 1);
        const bool hand0 = RawFrameSlotHasHand(raw, 0);
        const bool hand1 = RawFrameSlotHasHand(raw, 1);

        if (palm0 || palm1) {
            diag.any_input_frames += 1;
        }
        if (palm0 && palm1) {
            diag.both_palm_frames += 1;
        }
        if (hand0 && hand1) {
            diag.both_hand_frames += 1;
        } else if (hand0 || hand1) {
            diag.single_hand_frames += 1;
        } else {
            diag.no_hand_frames += 1;
        }

        const bool palms[kGestureNumHands] = {palm0, palm1};
        const bool hands[kGestureNumHands] = {hand0, hand1};
        for (int side = 0; side < kGestureNumHands; side++) {
            if (palms[side]) {
                diag.slot_palm_frames[side] += 1;
            }
            if (hands[side]) {
                diag.slot_hand_frames[side] += 1;
                current_missing_run[side] = 0;
            } else {
                current_missing_run[side] += 1;
                diag.max_hand_missing_run[side] =
                    std::max(diag.max_hand_missing_run[side], current_missing_run[side]);
            }
            if (palms[side] && !hands[side]) {
                diag.slot_palm_only_frames[side] += 1;
            }
        }
    }
    return diag;
}

void FULLCASCADEGESTURERECOGNIZER::PrintWindowDiagnostics(
    uint32_t frame_index, const WindowDiagnostics& diag) const {
    const float denom = static_cast<float>(std::max<uint32_t>(1, diag.frames_available));
    const float slot0_palm_ratio = diag.slot_palm_frames[0] / denom;
    const float slot1_palm_ratio = diag.slot_palm_frames[1] / denom;
    const float slot0_hand_ratio = diag.slot_hand_frames[0] / denom;
    const float slot1_hand_ratio = diag.slot_hand_frames[1] / denom;
    const float both_hand_ratio = diag.both_hand_frames / denom;
    const float no_hand_ratio = diag.no_hand_frames / denom;
    const float slot0_hand_given_palm =
        diag.slot_palm_frames[0] > 0
            ? static_cast<float>(diag.slot_hand_frames[0]) / diag.slot_palm_frames[0]
            : 0.0f;
    const float slot1_hand_given_palm =
        diag.slot_palm_frames[1] > 0
            ? static_cast<float>(diag.slot_hand_frames[1]) / diag.slot_palm_frames[1]
            : 0.0f;

    std::cout << "[FULLCASCADE][WINDOW] f=" << frame_index
              << " frames=" << diag.frames_available
              << " any=" << diag.any_input_frames
              << " slot0_palm=" << diag.slot_palm_frames[0] << "/" << diag.frames_available
              << "(" << std::fixed << std::setprecision(2) << slot0_palm_ratio << ")"
              << " slot1_palm=" << diag.slot_palm_frames[1] << "/" << diag.frames_available
              << "(" << slot1_palm_ratio << ")"
              << " slot0_hand=" << diag.slot_hand_frames[0] << "/" << diag.frames_available
              << "(" << slot0_hand_ratio << ")"
              << " slot1_hand=" << diag.slot_hand_frames[1] << "/" << diag.frames_available
              << "(" << slot1_hand_ratio << ")"
              << " both_palm=" << diag.both_palm_frames
              << " both_hand=" << diag.both_hand_frames << "(" << both_hand_ratio << ")"
              << " single_hand=" << diag.single_hand_frames
              << " no_hand=" << diag.no_hand_frames << "(" << no_hand_ratio << ")"
              << " palm_only=(" << diag.slot_palm_only_frames[0] << ","
              << diag.slot_palm_only_frames[1] << ")"
              << " hand_given_palm=(" << slot0_hand_given_palm << ","
              << slot1_hand_given_palm << ")"
              << " max_hand_gap=(" << diag.max_hand_missing_run[0] << ","
              << diag.max_hand_missing_run[1] << ")"
              << std::endl;
}

FULLCASCADEGESTURERECOGNIZER::ModelInputDiagnostics
FULLCASCADEGESTURERECOGNIZER::GetModelInputDiagnostics() const {
    ModelInputDiagnostics diag;
    double sum_x = 0.0, sum_y = 0.0, sum_x2 = 0.0, sum_y2 = 0.0;
    double sum_abs_x = 0.0, sum_abs_y = 0.0;
    uint64_t valid_count = 0;
    double slot_sum_x[kGestureNumHands] = {0.0, 0.0};
    double slot_sum_y[kGestureNumHands] = {0.0, 0.0};
    uint64_t slot_count[kGestureNumHands] = {0, 0};
    std::vector<float> motions;
    bool active[kFullCascadeWindowFrames] = {false};
    double spread_sum[kGestureNumHands] = {0.0, 0.0};
    uint32_t spread_count[kGestureNumHands] = {0, 0};
    double palm_w_sum[kGestureNumHands] = {0.0, 0.0};
    double palm_h_sum[kGestureNumHands] = {0.0, 0.0};
    uint32_t palm_box_count[kGestureNumHands] = {0, 0};

    const auto at = [this](int c, int j, int t) -> float {
        return input_buffer[((c * kFullCascadeNumJoints + j) * kFullCascadeWindowFrames) + t];
    };

    for (int t = 0; t < kFullCascadeWindowFrames; ++t) {
        for (int j = 0; j < kFullCascadeNumJoints; ++j) {
            if (at(2, j, t) <= 0.5f) {
                continue;
            }
            active[t] = true;
            const float x = at(0, j, t);
            const float y = at(1, j, t);
            sum_x += x; sum_y += y;
            sum_x2 += static_cast<double>(x) * x;
            sum_y2 += static_cast<double>(y) * y;
            sum_abs_x += std::fabs(x);
            sum_abs_y += std::fabs(y);
            valid_count += 1;
            const int side = j / kFullCascadePointsPerHand;
            slot_sum_x[side] += x;
            slot_sum_y[side] += y;
            slot_count[side] += 1;
        }
    }

    if (valid_count > 0) {
        const double mx = sum_x / valid_count;
        const double my = sum_y / valid_count;
        diag.x_abs_mean = static_cast<float>(sum_abs_x / valid_count);
        diag.y_abs_mean = static_cast<float>(sum_abs_y / valid_count);
        diag.x_std = static_cast<float>(std::sqrt(std::max(0.0, sum_x2 / valid_count - mx * mx)));
        diag.y_std = static_cast<float>(std::sqrt(std::max(0.0, sum_y2 / valid_count - my * my)));
    }
    for (int side = 0; side < kGestureNumHands; ++side) {
        if (slot_count[side] > 0) {
            diag.slot_x_mean[side] = static_cast<float>(slot_sum_x[side] / slot_count[side]);
            diag.slot_y_mean[side] = static_cast<float>(slot_sum_y[side] / slot_count[side]);
        }
    }

    for (int j = 0; j < kFullCascadeNumJoints; ++j) {
        for (int t = 1; t < kFullCascadeWindowFrames; ++t) {
            if (at(2, j, t - 1) <= 0.5f || at(2, j, t) <= 0.5f) {
                continue;
            }
            const float dx = at(0, j, t) - at(0, j, t - 1);
            const float dy = at(1, j, t) - at(1, j, t - 1);
            motions.push_back(std::sqrt(dx * dx + dy * dy));
        }
    }
    diag.motion_pairs = static_cast<uint32_t>(motions.size());
    if (!motions.empty()) {
        double total = 0.0;
        for (float v : motions) {
            total += v;
            diag.motion_max = std::max(diag.motion_max, v);
        }
        diag.motion_mean = static_cast<float>(total / motions.size());
        std::sort(motions.begin(), motions.end());
        const size_t p95_index = static_cast<size_t>(std::floor(0.95 * static_cast<double>(motions.size() - 1)));
        diag.motion_p95 = motions[p95_index];
    }

    int first8 = 0, last8 = 0;
    for (int t = 0; t < kFullCascadeWindowFrames; ++t) {
        if (active[t]) {
            if (diag.active_first < 0) diag.active_first = t;
            diag.active_last = t;
            if (t < 8) first8 += 1;
            if (t >= kFullCascadeWindowFrames - 8) last8 += 1;
        }
    }
    if (diag.active_first >= 0 && diag.active_last >= diag.active_first) {
        diag.active_span = diag.active_last - diag.active_first + 1;
    }
    diag.first8_active_ratio = first8 / 8.0f;
    diag.last8_active_ratio = last8 / 8.0f;

    for (int side = 0; side < kGestureNumHands; ++side) {
        const int base = side * kFullCascadePointsPerHand;
        for (int t = 0; t < kFullCascadeWindowFrames; ++t) {
            double hx = 0.0, hy = 0.0;
            int hc = 0;
            for (int pidx = 0; pidx < kHandNumLandmarks; ++pidx) {
                const int j = base + pidx;
                if (at(2, j, t) <= 0.5f) continue;
                hx += at(0, j, t);
                hy += at(1, j, t);
                hc += 1;
            }
            if (hc >= 5) {
                hx /= hc; hy /= hc;
                double sq = 0.0;
                for (int pidx = 0; pidx < kHandNumLandmarks; ++pidx) {
                    const int j = base + pidx;
                    if (at(2, j, t) <= 0.5f) continue;
                    const double dx = at(0, j, t) - hx;
                    const double dy = at(1, j, t) - hy;
                    sq += dx * dx + dy * dy;
                }
                spread_sum[side] += std::sqrt(sq / hc);
                spread_count[side] += 1;
            }

            const int p0 = base + kFullCascadePalmBoxPointStart + 0;
            const int p1 = base + kFullCascadePalmBoxPointStart + 1;
            const int p2 = base + kFullCascadePalmBoxPointStart + 2;
            const int p3 = base + kFullCascadePalmBoxPointStart + 3;
            if (at(2,p0,t) > 0.5f && at(2,p1,t) > 0.5f && at(2,p2,t) > 0.5f && at(2,p3,t) > 0.5f) {
                const float w1 = std::fabs(at(0,p1,t) - at(0,p0,t));
                const float w2 = std::fabs(at(0,p2,t) - at(0,p3,t));
                const float h1 = std::fabs(at(1,p3,t) - at(1,p0,t));
                const float h2 = std::fabs(at(1,p2,t) - at(1,p1,t));
                palm_w_sum[side] += 0.5 * (w1 + w2);
                palm_h_sum[side] += 0.5 * (h1 + h2);
                palm_box_count[side] += 1;
            }
        }
        if (spread_count[side] > 0) {
            diag.hand_spread_mean[side] = static_cast<float>(spread_sum[side] / spread_count[side]);
        }
        if (palm_box_count[side] > 0) {
            diag.palm_box_w_mean[side] = static_cast<float>(palm_w_sum[side] / palm_box_count[side]);
            diag.palm_box_h_mean[side] = static_cast<float>(palm_h_sum[side] / palm_box_count[side]);
        }
    }
    return diag;
}

void FULLCASCADEGESTURERECOGNIZER::PrintModelInputDiagnostics(
    uint32_t frame_index, const ModelInputDiagnostics& d) const {
    std::cout << "[FULLCASCADE][FEATURE] f=" << frame_index
              << " xabs=" << std::fixed << std::setprecision(3) << d.x_abs_mean
              << " yabs=" << d.y_abs_mean
              << " xstd=" << d.x_std
              << " ystd=" << d.y_std
              << " slot_x=(" << d.slot_x_mean[0] << "," << d.slot_x_mean[1] << ")"
              << " slot_y=(" << d.slot_y_mean[0] << "," << d.slot_y_mean[1] << ")"
              << " motion=(mean=" << d.motion_mean << ",p95=" << d.motion_p95
              << ",max=" << d.motion_max << ",pairs=" << d.motion_pairs << ")"
              << " active=(" << d.active_first << "," << d.active_last
              << ",span=" << d.active_span << ")"
              << " edge8=(" << d.first8_active_ratio << "," << d.last8_active_ratio << ")"
              << " hand_spread=(" << d.hand_spread_mean[0] << "," << d.hand_spread_mean[1] << ")"
              << " palm_wh0=(" << d.palm_box_w_mean[0] << "," << d.palm_box_h_mean[0] << ")"
              << " palm_wh1=(" << d.palm_box_w_mean[1] << "," << d.palm_box_h_mean[1] << ")"
              << std::endl;
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
    return RunInferencePacked(input_data, input_bytes, result);
}

bool FULLCASCADEGESTURERECOGNIZER::RunInferencePacked(const void* input_data,
                                                      int input_bytes,
                                                      GestureResult* result) {
    if (result == nullptr || input_data == nullptr || input_bytes <= 0) {
        return false;
    }
    if (ShouldSkipInferenceAfterFailures()) {
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
                  << ", configured_output_quant_scale=" << output_quant_scale
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
        float value = ReadTensorValue(outputs[0], output_info, static_cast<size_t>(i));
        if (output_info.dtype == SSNE_INT8) {
            value *= output_quant_scale;
        }
        result->logits[i] = value;
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
        // IMPORTANT: the ONNX model is logically [N,C,J,T] = [1,4,54,64],
        // but the converted SSNE/M1 runtime consumes the physical input buffer
        // with channels innermost: [J,T,C].  The Golden layout sweep proved
        // that canonical C-J-T bytes collapse short/thick to long, while the
        // exact same tensors packed as J-T-C restore 3/3 correct predictions.
        // Keep input_buffer in canonical C-J-T form for feature construction
        // and diagnostics, and transpose only at the runtime packing boundary.
        for (int j = 0; j < kFullCascadeNumJoints; j++) {
            for (int t = 0; t < kFullCascadeWindowFrames; t++) {
                for (int c = 0; c < kFullCascadeInputChannels; c++) {
                    const size_t src =
                        (static_cast<size_t>(c) * kFullCascadeNumJoints +
                         static_cast<size_t>(j)) *
                            kFullCascadeWindowFrames +
                        static_cast<size_t>(t);
                    const size_t dst =
                        (static_cast<size_t>(j) * kFullCascadeWindowFrames +
                         static_cast<size_t>(t)) *
                            kFullCascadeInputChannels +
                        static_cast<size_t>(c);
                    const float q = std::round(input_buffer[src] * input_quant_scale);
                    input_buffer_int8[dst] =
                        static_cast<int8_t>(Clamp(q, -128.0f, 127.0f));
                }
            }
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
    std::cout << "[FULLCASCADE] Input contract: INPUT0 source shape=1x4x54x64, float_bytes="
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
              << ", output_quant_scale=" << output_quant_scale
              << ", min_palm_score=" << min_palm_score
              << ", hand_confidence_feature_gate=disabled"
              << ", warmup_frames=" << warmup_frames
              << ", debug_interval=" << debug_interval
              << ", stable_hits=" << stable_hits
              << ", verbose_log=" << (verbose_log ? 1 : 0)
              << ", no_input_reset_frames=" << no_input_reset_frames
              << ", diagnostic_repeat=" << (diagnostic_repeat ? 1 : 0)
              << std::endl;
    std::cout << "[FULLCASCADE] Point layout per side: 0..20=hand, 21..24=palm_box, "
                 "25=palm_p0, 26=palm_p9"
              << std::endl;
    std::cout << "[FULLCASCADE] Classes: 0=rain, 1=long, 2=short, 3=go, 4=thick, "
                 "5=no_gesture"
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
                  << ", model_box_tlbr=(" << det.model_box[0] << "," << det.model_box[1]
                  << "," << det.model_box[2] << "," << det.model_box[3] << ")"
                  << ", p0=(" << det.keypoints[0].pixel_x << "," << det.keypoints[0].pixel_y
                  << ")"
                  << ", model_p0=(" << det.keypoints[0].model_x << ","
                  << det.keypoints[0].model_y << ")"
                  << ", p9=(" << det.keypoints[1].pixel_x << "," << det.keypoints[1].pixel_y
                  << ")"
                  << ", model_p9=(" << det.keypoints[1].model_x << ","
                  << det.keypoints[1].model_y << ")"
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
