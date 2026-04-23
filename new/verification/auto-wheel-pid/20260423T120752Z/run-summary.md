# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T12:09:24Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-baseline100-20260423T063744Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-both-d-l0_75-r0_1-working-20260423T1215Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-both-d-l0_75-r0_1-best-20260423T1215Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `inf`
- right: `1.635153`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`inf` right_score=`1.635153`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`1.032172` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/baseline-screen/attempt-01/board.log`
- `round-001-d-screen` candidate=`left-d=0.750000; right-d=0.100000` accepted=`False` reason=`screen left score 1.077101 did not beat 1.032172 by 5%; screen right score improved from inf to 1.140895; pending confirmation` left_score=`1.077101` right_score=`1.140895`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/round-001-d-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/round-001-d-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/round-001-d-screen/attempt-01/board.log`
- `round-002-d-confirm` candidate=`right-d=0.100000` accepted=`False` reason=`confirm right terminal gate failed: terminal gate failed` left_score=`1.154595` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/round-002-d-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/round-002-d-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120752Z/round-002-d-confirm/attempt-01/board.log`
