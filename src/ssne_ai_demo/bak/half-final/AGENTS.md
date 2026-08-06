# AGENTS.md

## I. Purpose

This file defines the repository-wide instructions for coding agents.

Agents must follow these instructions when inspecting, modifying, testing, or
documenting this repository.

The primary goals are:

1. Preserve repository correctness and maintainability.
2. Make only changes required by the current task.
3. Avoid destructive, unrelated, or speculative modifications.

---

## II. Instruction Priority

Follow instructions in this order:

1. Explicit instructions in the current user request.
2. The nearest applicable `AGENTS.override.md`.
3. The nearest applicable `AGENTS.md`.
4. This repository-level `AGENTS.md`.
5. Existing project conventions inferred from nearby code.

Higher-priority instructions override lower-priority instructions.

Do not interpret a previous task's temporary authorization as authorization for
the current task.

When two applicable instructions conflict and the conflict cannot be resolved
safely, stop before modifying files and explain the conflict.

---

## III. Repository Overview

### 3.1 Project purpose

This project is a sign language recognition system built on the A1 NPU platform, designed to detect and interpret sign language gestures in real-time. Following functions are implemented:

1. **Palm Detection**: Identifies the presence and location of hands in the camera feed.
2. **Hand Landmarking**: Extracts key points from detected hands to understand their positions and movements.
3. **Gloss Translation**: Converts sequences of hand gestures into isolated word classifications.


### 3.2 Main technologies

This folder is dedicated solely to deploying models—previously trained using TensorFlow—onto the A1 development board; the project is primarily implemented in C++ and is unrelated to model training or evaluation.

### Important directories

| Path              | Purpose                                |
| ----------------- | -------------------------------------- |
| `<src/>`          | Main source code                       |
| `<include/>`      | Header files                           |
| `<cmake_config/>` | config files of CMake                  |
| `<app_assets/>`   | model file and other assets            |
| `<examples/>`     | Example programs or usage              |
| `<scripts/>`      | Automation scripts                     |
| `*.md*`           | Documentation files;                   |

---

### IV. General Working Rules

#### 4.1 Docs Modifying Rules

1. `PALM_DEBUGGING_NOTES.md`: Agents are prohibited from modifying this file.
2. `README.md`: Agents are permitted to make modifications (such as adjusting system compilation, execution, or quick-run methods), provided that they do not significantly disrupt the outline or subheading structure, avoid making drastic changes in a single pass, and do not delete content unrelated to the current task.
3. `half-final/SLR_WORKFLOW.md`: This section outlines the workflow of the edge-side scheduler for the sign language recognition system. Whenever the program or script is updated following the completion of a task, this document must be revised to reflect those changes.
4. `DEVICE_3MODEL_DATAFLOW.md`: This section describes the data flow of the three models on the device. Whenever the data flow is updated following the completion of a task, this document must be revised to reflect those changes.
5. `PERFORMANCE_MONITOR_USAGE.md`: This section describes the usage of the performance monitor. Whenever the performance monitor is updated following the completion of a task, this document must be revised to reflect those changes.