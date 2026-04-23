# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T12:24:59Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-p100-dformal-l0_75-r0_20-20260423T1220Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-iscan-working-20260423T1220Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-iscan-best-20260423T1220Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `0.680752`
- right: `inf`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`1.974867` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/baseline-screen/attempt-01/board.log`
- `round-001-i-screen` candidate=`left-i=0.050000; right-i=0.050000` accepted=`False` reason=`screen left score 1.995285 did not beat 1.974867 by 5%; screen right score improved from inf to 1.995714; pending confirmation` left_score=`1.995285` right_score=`1.995714`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-001-i-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-001-i-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-001-i-screen/attempt-01/board.log`
- `round-002-i-screen` candidate=`left-i=0.100000; right-i=0.100000` accepted=`False` reason=`screen left score improved from 1.974867 to 1.014241; pending confirmation; screen right terminal gate failed: terminal gate failed` left_score=`1.014241` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-002-i-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-002-i-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-002-i-screen/attempt-01/board.log`
- `round-003-i-screen` candidate=`left-i=0.200000; right-i=0.200000` accepted=`False` reason=`screen left score 1.910416 did not beat 1.974867 by 5%; screen right terminal gate failed: tail PWM jump 914.333 > 800.000` left_score=`1.910416` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-003-i-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-003-i-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-003-i-screen/attempt-01/board.log`
- `round-004-i-screen` candidate=`left-i=0.400000; right-i=0.400000` accepted=`False` reason=`screen left terminal gate failed: tail PWM jump 894.333 > 800.000; screen right score improved from inf to 1.424364; pending confirmation` left_score=`inf` right_score=`1.424364`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-004-i-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-004-i-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-004-i-screen/attempt-01/board.log`
- `round-005-i-screen` candidate=`left-i=0.800000; right-i=0.800000` accepted=`False` reason=`screen left score improved from 1.974867 to 0.809008; pending confirmation; screen right score improved from inf to 1.214210; pending confirmation` left_score=`0.809008` right_score=`1.214210`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-005-i-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-005-i-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-005-i-screen/attempt-01/board.log`
- `round-006-i-confirm` candidate=`left-i=0.800000; right-i=0.800000` accepted=`True` reason=`left score improved from inf to 0.680752; selected best candidate after paired screen+confirm at left-i=0.800000` left_score=`0.680752` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-006-i-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-006-i-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T122234Z/round-006-i-confirm/attempt-01/board.log`
