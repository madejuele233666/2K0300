# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T11:41:16Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-baseline100-20260423T063744Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-fine-99_5-working-20260423T1145Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-fine-99_5-best-20260423T1145Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `inf`
- right: `inf`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`1.766821` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/baseline-screen/attempt-01/board.log`
- `round-001-p-screen` candidate=`left-p=99.500000; right-p=99.500000` accepted=`False` reason=`screen left score 2.253825 did not beat 1.766821 by 5%; screen right score improved from inf to 2.200105; pending confirmation` left_score=`2.253825` right_score=`2.200105`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/round-001-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/round-001-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/round-001-p-screen/attempt-01/board.log`
- `round-002-p-confirm` candidate=`right-p=99.500000` accepted=`False` reason=`confirm right terminal gate failed: terminal gate failed` left_score=`1.919724` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/round-002-p-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/round-002-p-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T113942Z/round-002-p-confirm/attempt-01/board.log`
