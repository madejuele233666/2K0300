# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T12:16:08Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-baseline100-20260423T063744Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-both-d-l0_75-r0_15-working-20260423T1225Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-both-d-l0_75-r0_15-best-20260423T1225Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `inf`
- right: `1.451083`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`inf` right_score=`1.451083`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/baseline-screen/attempt-01/board.log`
- `round-001-d-screen` candidate=`left-d=0.750000; right-d=0.150000` accepted=`False` reason=`screen left score improved from inf to 0.948138; pending confirmation; screen right score improved from inf to 2.047543; pending confirmation` left_score=`0.948138` right_score=`2.047543`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/round-001-d-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/round-001-d-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/round-001-d-screen/attempt-01/board.log`
- `round-002-d-confirm` candidate=`left-d=0.750000; right-d=0.150000` accepted=`False` reason=`confirm left terminal gate failed: terminal gate failed; confirm right terminal gate failed: terminal gate failed` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/round-002-d-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/round-002-d-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121437Z/round-002-d-confirm/attempt-01/board.log`
