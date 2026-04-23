# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T12:28:34Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-p100-dformal-l0_75-r0_20-20260423T1220Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-iscan-up-working-20260423T1230Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-iscan-up-best-20260423T1230Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `0.929162`
- right: `1.673091`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`1.394945` right_score=`1.673091`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`1.967084` right_score=`1.326731`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/baseline-screen/attempt-01/board.log`
- `round-001-i-screen` candidate=`left-i=1.200000; right-i=1.200000` accepted=`False` reason=`screen left terminal gate failed: terminal gate failed; screen right score 1.581416 did not beat 1.326731 by 5%` left_score=`inf` right_score=`1.581416`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-001-i-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-001-i-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-001-i-screen/attempt-01/board.log`
- `round-002-i-screen` candidate=`left-i=1.600000; right-i=1.600000` accepted=`False` reason=`screen left terminal gate failed: terminal gate failed; screen right terminal gate failed: tail PWM jump 823.619 > 800.000` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-002-i-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-002-i-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-002-i-screen/attempt-01/board.log`
- `round-003-i-screen` candidate=`left-i=2.000000; right-i=2.000000` accepted=`False` reason=`screen left terminal gate failed: terminal gate failed; screen right score improved from 1.326731 to 1.065761; pending confirmation` left_score=`inf` right_score=`1.065761`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-003-i-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-003-i-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-003-i-screen/attempt-01/board.log`
- `round-004-i-screen` candidate=`left-i=2.400000; right-i=2.400000` accepted=`False` reason=`screen left score improved from 1.967084 to 0.621641; pending confirmation; screen right terminal gate failed: tail PWM jump 893.810 > 800.000` left_score=`0.621641` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-004-i-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-004-i-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-004-i-screen/attempt-01/board.log`
- `round-005-i-confirm` candidate=`left-i=2.400000; right-i=2.000000` accepted=`True` reason=`left score improved from 1.394945 to 0.929162; selected best candidate after paired screen+confirm at left-i=2.400000` left_score=`0.929162` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-005-i-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-005-i-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122618Z/round-005-i-confirm/attempt-01/board.log`
