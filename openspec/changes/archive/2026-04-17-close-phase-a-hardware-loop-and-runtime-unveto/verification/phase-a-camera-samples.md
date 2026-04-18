# Phase A Camera Samples

This note exists only to capture the direct-match sample-policy conclusion from the `2026-04-17` board checks.

## Sample Conclusions

1. Direct-match accepts the current Phase A geometry contract only at the expected `160x120 -> 160x128` path.
2. A forced non-Phase1 geometry does not silently pass through as if it were the mainline sample shape.
3. A non-default exposure request is rejected before Phase A can pretend that real exposure control exists on the direct-match path.
