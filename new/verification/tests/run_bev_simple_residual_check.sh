#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT_DIR"

patterns=(
  "Reference""PolicyState"
  "reference""_policy"
  "Analyze""Frame"
  "capture""\\.frame([^_[:alnum:]]|$)"
  "latest""_camera_capture"
  "ResetSteering""TrackMemory"
  "kBev""TrackSampleCount"
  "kCenter""line"
  "center""line"
  "bias""_m"
  "left""_edge_offset"
  "right""_edge_offset"
  "Left""EdgeOffset"
  "Right""EdgeOffset"
  "reference_hold""\\.valid"
  "BEVReferencePath::""valid"
  "BEVPathSample::""valid"
  "BEVPathPointSource::k""Invalid"
  "ReferenceCurvatureEstimate::""valid"
  "BEVSimplePerceptionResult::""valid"
  "reference_path""\\.valid"
  "reference_curvature""\\.valid"
  "ReferenceCurvature""Input"
  "ControlConstraint""Set"
  "control""_constraints"
  "CameraTurn""Computation"
  "camera""_turn"
  "camera_error""_last"
  "camera_p""_term"
  "camera_d""_term"
  "threshold""_veto"
  "geometry""_veto"
  "track""_valid"
  "active""_module"
  "scene""_phase"
  "scene""_override_source"
  "NOMINAL""_LANE_WIDTH_M"
  "MIN""_LANE_WIDTH_M"
  "MAX""_LANE_WIDTH_M"
  "CONTINUITY""_BREAK_THRESHOLD_M"
  "SAMPLE""_ROW_STEP_PX"
  "IMAGE""_BORDER_TRUNCATION_MARGIN_PX"
  "BEV_""TOPOLOGY"
  "BEV_""PATH_POLICY"
  "trusted""_error"
  "circle""_inner"
  "cross""_band"
  "w_""target"
  "CURVATURE_TO_W_""TARGET_GAIN"
  "assistant_image""_publish_interval"
  "assistant_waveform""_publish_interval"
  "SendAssistant""Oscilloscope"
  "Publish""Waveform"
  "SendAssistant""Image"
  "BEV image ""->"
  "control ""error"
  "#include \"port/control_types""\\.hpp\""
  "#include <port/control_types""\\.hpp>"
  "Perception""Result.*struct"
  "struct Perception""Result"
  "Reference""Usability.*struct"
  "struct Reference""Usability"
  "Reference""CurvatureEstimate.*struct"
  "struct Reference""CurvatureEstimate"
  "Control""Readiness.*struct"
  "struct Control""Readiness"
  "Speed""_base"
  "see""_max"
  "PID_TURN_GYRO""_CAMERA"
  "pid_turn_gyro""_camera"
  "roadblock""_active"
  "roadblock""_interface_state"
  "AnalyzeSimple""BevFrame"
  "SteeringFrame""Analysis"
  "ResolveReference""Continuity"
  "imu""_grace_active"
  "gyro""_continuity"
  "ComputeGyro""ContinuityConstraint"
  "steering""_gyro""_continuity"
  "ComputeEffective""SpeedTargetForState"
  "Legacy""AttitudeLogic"
  "attitude""_logic\\.cpp"
  "sample\\.source == port::BEVPathPointSource::k""None"
  "BEV""ReferencePath.*struct"
  "struct BEV""ReferencePath"
  "Legacy""SteeringState.*struct"
  "struct Legacy""SteeringState"
  "emergency""_veto"
  "perception_emergency""_veto"
  "kPerception""EmergencyVeto"
  "ReferenceHas""HoldSource"
  "ComputeYawRateTarget\\(const port::Perception""Result&"
  "steering_control""_error_model"
  "control_error""_model_test"
  "Legacy""PidControl"
  "ReferencePath""BuildOptions"
  "pid_turn_""camera"
  "P_""Mode"
  "Fuzz""yPidUcas"
  "fuzz""y_pid_ucas"
  "emergency""_threshold"
  "Vehicle""Context"
  "steering""_path_math"
  "BEVSimplePerceptionResult::""bev"
  "result\\.bev"
)

regex="$(IFS='|'; echo "${patterns[*]}")"

if rg -n "$regex" \
  new/code \
  new/user \
  new/config \
  new/verification/tests \
  openspec/changes \
  -g '!new/code/archive/**' \
  -g '!new/verification/archive/**' \
  -g '!openspec/changes/archive/**' \
  -g '!openspec/changes/add-simcar-3d-local-simulation/**' \
  -g '!new/code/port/README.md' \
  -g '!new/code/port/perception_result.hpp' \
  -g '!new/code/port/reference_usability_types.hpp' \
  -g '!new/code/port/reference_lateral_error_types.hpp' \
  -g '!new/code/port/reference_control_readiness_types.hpp' \
  -g '!new/code/port/bev_reference_types.hpp' \
  -g '!new/code/port/steering_state_types.hpp'; then
  echo "bev_simple_residual_check failed: removed BEV contract residue is still present" >&2
  exit 1
fi

if [[ -e new/code/port/control_types.hpp ]]; then
  echo "bev_simple_residual_check failed: new/code/port/control_types.hpp must not exist" >&2
  exit 1
fi

if [[ -e new/code/port/reference_curvature_types.hpp ||
      -e new/code/legacy/steering_reference_curvature.cpp ||
      -e new/code/legacy/steering_reference_curvature.hpp ]]; then
  echo "bev_simple_residual_check failed: removed reference curvature files must not exist" >&2
  exit 1
fi

if ! grep -q "Do not create aggregate include headers" new/code/port/README.md; then
  echo "bev_simple_residual_check failed: port include boundary README is missing aggregate-header rule" >&2
  exit 1
fi

if ! grep -q "PerceptionResult is a runtime transport snapshot" new/code/port/README.md; then
  echo "bev_simple_residual_check failed: port include boundary README is missing PerceptionResult rule" >&2
  exit 1
fi

if ! grep -q "new/code/port/README.md" new/config/default_params.md; then
  echo "bev_simple_residual_check failed: default_params.md must reference port include boundary docs" >&2
  exit 1
fi

if [[ ! -f new/verification/tests/run_runtime_parameter_defaults_test.sh ]]; then
  echo "bev_simple_residual_check failed: runtime parameter defaults contract test must exist" >&2
  exit 1
fi

if [[ ! -f new/verification/test-images/authority-baseline/README.md ]]; then
  echo "bev_simple_residual_check failed: authority-baseline README must define historical overlay boundary" >&2
  exit 1
fi

if ! grep -q "historical manifest, overlay, and txt outputs" new/verification/test-images/authority-baseline/README.md; then
  echo "bev_simple_residual_check failed: authority-baseline README must mention historical manifest/overlay/txt outputs" >&2
  exit 1
fi

if ! grep -q "must not be used as active authority" new/verification/test-images/authority-baseline/README.md; then
  echo "bev_simple_residual_check failed: authority-baseline README must forbid historical outputs as active authority" >&2
  exit 1
fi

if [[ -f new/verification/test-images/authority-baseline/manifest.json ]]; then
  echo "bev_simple_residual_check failed: active authority-baseline manifest must be archived" >&2
  exit 1
fi

if [[ ! -f new/verification/archive/pre_simple_reference_control_contract/authority-baseline/manifest.json ]]; then
  echo "bev_simple_residual_check failed: historical authority-baseline manifest must exist in archive" >&2
  exit 1
fi

if ! grep -q "historical manifest" new/verification/test-images/authority-baseline/README.md; then
  echo "bev_simple_residual_check failed: authority-baseline README must mention historical manifest" >&2
  exit 1
fi

if ! grep -q '"low_voltage_raw_threshold"' new/code/platform/param_store.cpp; then
  echo "bev_simple_residual_check failed: param_store.cpp must read low_voltage_raw_threshold" >&2
  exit 1
fi

legacy_low_voltage_key='"emergency''_threshold"'
if grep -q "$legacy_low_voltage_key" new/code/platform/param_store.cpp; then
  echo "bev_simple_residual_check failed: param_store.cpp must not read emergency""_threshold" >&2
  exit 1
fi

if ! grep -q "low_voltage_raw_threshold" new/code/platform/steering_media_protocol.hpp ||
   ! grep -q "low_voltage_raw_threshold" new/code/platform/steering_media_protocol.cpp; then
  echo "bev_simple_residual_check failed: steering media config snapshot must expose low_voltage_raw_threshold" >&2
  exit 1
fi

if ! grep -Fq 'element_evidence.records[' new/user/scene_overlay_probe.cpp; then
  echo "bev_simple_residual_check failed: scene_overlay_probe must expose generic element evidence records" >&2
  exit 1
fi

if rg -n "source ==|source !=|mode == port::ReferenceMode|mode != port::ReferenceMode|switch \\(mode\\)|ReferenceMode" \
  new/code/runtime \
  new/code/legacy/steering_reference_control_readiness.cpp \
  new/code/legacy/steering_reference_usability.cpp \
  new/code/legacy/steering_reference_lateral_error.cpp \
  new/code/legacy/steering_yaw_controller.cpp; then
  echo "bev_simple_residual_check failed: control/usability/lateral-error/yaw layers must not decide from source or mode" >&2
  exit 1
fi

if rg -n "port::Perception""Result|perception_result\\.hpp" \
  new/code/legacy \
  -g '!new/code/archive/**'; then
  echo "bev_simple_residual_check failed: legacy analysis must not include or construct Perception""Result" >&2
  exit 1
fi

if rg -n "perception_result\\.hpp" \
  new/code/legacy/steering_reference_usability.cpp \
  new/code/legacy/steering_reference_usability.hpp \
  new/code/legacy/steering_reference_lateral_error.cpp \
  new/code/legacy/steering_reference_lateral_error.hpp \
  new/code/legacy/steering_yaw_controller.cpp \
  new/code/legacy/steering_yaw_controller.hpp; then
  echo "bev_simple_residual_check failed: reference/yaw layers must not include perception_result.hpp" >&2
  exit 1
fi

if rg -n "Reference""Usability|reference""_usability" \
  new/code/legacy/steering_bev_simple_perception.cpp \
  new/code/legacy/steering_bev_simple_perception.hpp; then
  echo "bev_simple_residual_check failed: simple BEV perception and hold builders must not depend on reference usability" >&2
  exit 1
fi

if rg -n "ComputeEffective""SpeedTargetForState|ResolveRuntime""SpeedTarget" \
  new/code/runtime/assistant_service.cpp; then
  echo "bev_simple_residual_check failed: assistant service must serialize published speed facts, not recompute them" >&2
  exit 1
fi

if rg -n "imu_valid" \
  new/code/legacy/steering_yaw_controller.cpp \
  new/code/legacy/steering_yaw_controller.hpp; then
  echo "bev_simple_residual_check failed: yaw controller must not receive or branch on IMU validity" >&2
  exit 1
fi

if rg -n -U "ComputeGyroTurn\\([^)]*imu_valid" \
  new/code \
  new/user \
  new/verification/tests \
  -g '!new/code/archive/**' \
  -g '!new/verification/archive/**'; then
  echo "bev_simple_residual_check failed: ComputeGyroTurn must not use the old validity-aware signature" >&2
  exit 1
fi

echo "bev_simple_residual_check passed"
