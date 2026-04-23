# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T12:05:30Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-baseline100-20260423T063744Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-both-dscan-working-20260423T1200Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-both-dscan-best-20260423T1200Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `1.169526`
- right: `1.688775`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`1.169526` right_score=`1.688775`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`2.016709` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/baseline-screen/attempt-01/board.log`
- `round-001-d-screen` candidate=`left-d=0.250000; right-d=0.250000` accepted=`False` reason=`screen left terminal gate failed: terminal gate failed; screen right terminal gate failed: tail PWM jump 895.905 > 800.000` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-001-d-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-001-d-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-001-d-screen/attempt-01/board.log`
- `round-002-d-screen` candidate=`left-d=0.500000; right-d=0.500000` accepted=`False` reason=`screen left score improved from 2.016709 to 1.380695; pending confirmation; screen right terminal gate failed: terminal gate failed` left_score=`1.380695` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-002-d-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-002-d-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-002-d-screen/attempt-01/board.log`
- `round-003-d-screen` candidate=`left-d=0.750000; right-d=0.750000` accepted=`False` reason=`screen left score improved from 2.016709 to 0.833109; pending confirmation; screen right terminal gate failed: terminal gate failed` left_score=`0.833109` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-003-d-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-003-d-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-003-d-screen/attempt-01/board.log`
- `round-004-d-screen` candidate=`left-d=1.000000; right-d=1.000000` accepted=`False` reason=`screen left terminal gate failed: terminal gate failed; screen right terminal gate failed: terminal gate failed` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-004-d-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-004-d-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-004-d-screen/attempt-01/board.log`
- `round-005-d-confirm` candidate=`left-d=0.750000` accepted=`False` reason=`confirm left terminal gate failed: terminal gate failed` left_score=`inf` right_score=`1.797167`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-005-d-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-005-d-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120317Z/round-005-d-confirm/attempt-01/board.log`
