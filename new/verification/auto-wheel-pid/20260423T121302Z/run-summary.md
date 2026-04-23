# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T12:14:37Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-baseline100-20260423T063744Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-both-d-l0_70-r0_15-working-20260423T1225Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-both-d-l0_70-r0_15-best-20260423T1225Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `1.437570`
- right: `inf`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`1.437570` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`2.323050` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/baseline-screen/attempt-01/board.log`
- `round-001-d-screen` candidate=`left-d=0.700000; right-d=0.150000` accepted=`False` reason=`screen left score improved from 2.323050 to 1.575409; pending confirmation; screen right terminal gate failed: terminal gate failed` left_score=`1.575409` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/round-001-d-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/round-001-d-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/round-001-d-screen/attempt-01/board.log`
- `round-002-d-confirm` candidate=`left-d=0.700000` accepted=`False` reason=`confirm left terminal gate failed: terminal gate failed` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/round-002-d-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/round-002-d-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T121302Z/round-002-d-confirm/attempt-01/board.log`
