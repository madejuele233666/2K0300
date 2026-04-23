# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T12:50:44Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-pformal-iformal-l2_4-r2_2-20260423T1248Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-pscan-down-working-20260423T1248Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-pscan-down-best-20260423T1248Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `0.790248`
- right: `inf`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`0.829382` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/baseline-screen/attempt-01/board.log`
- `round-001-p-screen` candidate=`left-p=84.000000; right-p=84.000000` accepted=`False` reason=`screen left score improved from 0.829382 to 0.459758; pending confirmation; screen right terminal gate failed: terminal gate failed` left_score=`0.459758` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-001-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-001-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-001-p-screen/attempt-01/board.log`
- `round-002-p-screen` candidate=`left-p=88.000000; right-p=88.000000` accepted=`False` reason=`screen left score 0.847094 did not beat 0.829382 by 5%; screen right terminal gate failed: terminal gate failed` left_score=`0.847094` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-002-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-002-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-002-p-screen/attempt-01/board.log`
- `round-003-p-screen` candidate=`left-p=92.000000; right-p=92.000000` accepted=`False` reason=`screen left score 0.909337 did not beat 0.829382 by 5%; screen right terminal gate failed: terminal gate failed` left_score=`0.909337` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-003-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-003-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-003-p-screen/attempt-01/board.log`
- `round-004-p-screen` candidate=`left-p=96.000000; right-p=96.000000` accepted=`False` reason=`screen left score 1.241518 did not beat 0.829382 by 5%; screen right score improved from inf to 1.480732; pending confirmation` left_score=`1.241518` right_score=`1.480732`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-004-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-004-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-004-p-screen/attempt-01/board.log`
- `round-005-p-confirm` candidate=`left-p=84.000000; right-p=96.000000` accepted=`True` reason=`left score improved from inf to 0.790248; selected best candidate after paired screen+confirm at left-p=84.000000` left_score=`0.790248` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-005-p-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-005-p-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T124827Z/round-005-p-confirm/attempt-01/board.log`
