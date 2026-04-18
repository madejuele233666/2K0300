# Codex Adapter/Bootstrap Review (Task 2.6)

Scope reviewed:

- `new/code/port/**/*.hpp`
- `new/code/platform/**/*.cpp`
- `new/code/runtime/startup.cpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/runtime/shutdown.cpp`
- `new/user/main.cpp`
- `new/docs/debugging.md`
- `new/docs/hardware-matrix.md`

Result:

- no findings

Checks performed:

- no LS2K/OpenCV concrete type leakage in public `new/code/port/*.hpp`
- `new/user/main.cpp` remains a restricted composition root and hands off through project-owned contracts
- startup fail-safe gates actuator arming on startup-critical parameter application and adapter readiness
- camera contract explicitly enforces `160x120 -> 160x128` adaptation plus marker path for non-phase-1 geometry
- foreground perception publication remains outside PIT callback; periodic control consumes published state only
- hardware profile modes (`direct-match`, `adaptation-hook`, `disabled`) are explicit and adapter-consumed
