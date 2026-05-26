# New Workspace Docs

The current BEV reference/control contract is no longer described by the old race-finish roadmap documents.

Use these active documents instead:

- Root `README.md`: rules for extending the current simple BEV reference pipeline.
- `new/docs/visual-element-sparse-circle-v4.zh-CN.md`: V4 ordinary-reference lost-boundary fix contract; handles single-side lost line with nominal half-width and delegates double-side loss to existing hold continuity.
- `new/docs/visual-element-sparse-circle-v4-single-boundary-helper.zh-CN.md`: V4 appendix for the reusable single-boundary signed-normal-offset helper shared by ordinary lost-line repair and single-boundary scene path generation.
- `new/docs/visual-element-sparse-circle-v3.zh-CN.md`: V3 original entrance-line completion idea, using the circle-side outer entrance corner to construct a virtual opposite boundary for natural circle entry.
- `new/docs/visual-element-sparse-circle-v2.zh-CN.md`: V2 minimal circle state machine, preserving original circle detection and removing rear-black entry judgment.
- `new/docs/visual-element-sparse-circle-v1.zh-CN.md`: V1 sparse-first cross/circle visual element architecture.
- `new/config/default_params.md`: current runtime parameter contract.
- `new/code/port/README.md`: port type and include boundaries.
- `new/user/README.md`: build, deploy, steering evidence, and board workflow.
- `new/verification/test-images/authority-baseline/README.md`: current authority-baseline asset boundary.

Historical documents:

- `new/docs/superseded/race-finish-series.zh-CN/` contains the old race-finish phase roadmap.
- `new/docs/superseded/temp/` contains old draft plans.
- `new/docs/superseded/race-finish-series-source/` contains older source material absorbed by the former roadmap.

Historical documents are preserved as background only. They must not be used as active runtime, parameter, media, overlay, or verification authority.
