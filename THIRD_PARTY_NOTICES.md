# Third-Party Notices

AetherSign is an open-source research and engineering project developed by the PeakDragonSoar team.

Unless otherwise stated, original AetherSign source code is licensed under the Apache License 2.0. The AetherSign license applies only to materials for which the AetherSign contributors have the right to grant such a license.

Third-party software, models, datasets, SDKs, pretrained weights, trademarks, logos, and other external assets remain subject to their respective licenses and terms.

## 1. Vendor SDK and Hardware Toolchain

### SmartSens / A1 Vision SDK

AetherSign relies on the vendor-provided A1 Vision / SmartSens SDK for device-side compilation, NPU inference, camera access, and OSD functionality.

The vendor SDK, SDK headers, libraries, toolchain, firmware, and related proprietary components are **not distributed as part of AetherSign** and are **not covered by the Apache License 2.0 license of this repository**.

Users must obtain these components separately from the corresponding vendor or competition distribution channel and comply with their applicable license terms.

## 2. Open-Source Projects and Teacher Models

The following third-party projects were used as dependencies, teacher models, evaluation references, or architectural references during the development of AetherSign.

### Google MediaPipe

* Upstream project: `google-ai-edge/mediapipe`
* Upstream license: Apache License 2.0
* Role in AetherSign: hand detection / landmark estimation reference and teacher model; also used by the HLMF data annotation pipeline.
* Redistribution status: upstream source code is not vendored into the AetherSign main repository.

### MMPose / RTMPose

* Upstream project: `open-mmlab/mmpose`
* Upstream license: Apache License 2.0
* Role in AetherSign: hand landmark teacher model during dataset construction and benchmark evaluation.
* Redistribution status: upstream source code and pretrained weights are not distributed by the AetherSign main repository.

### HaMeR

* Upstream project: `geopavlakos/hamer`
* Upstream license: MIT License
* Role in AetherSign: teacher model and benchmark/reference model for difficult and occluded hand samples.
* Redistribution status: upstream source code, MANO assets, and pretrained weights are not distributed by the AetherSign main repository.

### Hamba

* Upstream project: `humansensinglab/Hamba`
* Upstream license: Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0), according to the upstream repository.
* Role in AetherSign: benchmark/reference model for hand landmark estimation.
* Redistribution status: upstream source code and pretrained weights are not distributed by the AetherSign main repository.

Use of Hamba remains subject to its upstream non-commercial terms. The Apache License 2.0 of AetherSign does not grant any additional rights to Hamba.

### SAM-SLR / CVPR21Chal-SLR / SSTCN

* Upstream project: `jackyjsy/CVPR21Chal-SLR`
* Role in AetherSign: reference for skeleton-based temporal modeling and isolated sign-language classification.
* Upstream terms: the upstream repository identifies CC0-1.0 while also stating additional academic-research-only and non-commercial conditions.
* Redistribution status: upstream source code is not redistributed as part of the AetherSign main repository.

Users should consult and comply with the upstream repository terms before reusing the corresponding implementation.

## 3. Training Frameworks and Runtime Libraries

The AetherSign model-development repositories rely on external machine-learning frameworks and runtime libraries, including but not limited to:

* TensorFlow / Keras
* PyTorch / torchvision
* ONNX
* ONNX Runtime
* OpenCV
* MediaPipe

These libraries are installed separately as software dependencies and remain under their respective upstream licenses.

The presence of a dependency does not imply that the dependency itself is relicensed under AetherSign's Apache License 2.0.

## 4. Datasets

AetherSign does **not** redistribute the original third-party sign-language datasets used during research and development.

Datasets referenced by the project include SLR500, CSL-Daily, CE-CSL, RWTH-BOSTON-104, How2Sign-derived resources, MS-ASL, WLASL, and other datasets listed in the project documentation.

These datasets remain subject to their respective release agreements, licenses, research-use restrictions, and citation requirements.

The Apache License 2.0 of AetherSign does not apply to those datasets or to any third-party dataset content.

## 5. Models and Pretrained Weights

Third-party pretrained model weights are not automatically covered by the source-code license of the corresponding software project.

Whenever external pretrained weights, model assets, MANO resources, or similar files are required, users must obtain them from the original provider and comply with the associated terms.

Unless explicitly stated otherwise, such third-party model assets are not distributed as part of AetherSign.

## 6. Competition Materials, Logos, and Trademarks

Third-party trademarks, organization names, competition logos, company logos, screenshots, product names, and similar materials that may appear in archived documentation remain the property of their respective owners.

Their presence in archival project materials does not imply that they are licensed under the Apache License 2.0.

## 7. No Endorsement

References to third-party projects, organizations, products, or datasets are provided for attribution, reproducibility, and documentation purposes only.

No endorsement by or affiliation with the corresponding third-party copyright holders is implied.

---

If any third-party material has been inadvertently omitted from this notice, please open an issue so that the attribution and licensing information can be corrected.
