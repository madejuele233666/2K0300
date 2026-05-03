# BEV Topology Pre-Simple Rewrite Archive

This directory preserves the removed BEV topology/corridor/reference-policy implementation for historical reference only.

Archived code here is intentionally inactive:

- It is not listed in `new/user/CMakeLists.txt`.
- It is not part of the runtime pipeline.
- It may reference DTOs and parameters removed by the simple BEV rewrite.
- Do not include these headers from active code.

Current active BEV line following starts from:

- `new/code/legacy/steering_bev_simple_perception.*`
- `new/code/legacy/steering_control_error_model.*`
- `new/code/legacy/camera_logic.cpp`
