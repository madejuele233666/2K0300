# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T11:43:48Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-baseline100-20260423T063744Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-fine-100_5-working-20260423T1145Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-fine-100_5-best-20260423T1145Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `1.260977`
- right: `inf`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`inf` right_score=`1.967518`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/baseline-screen/attempt-01/board.log`
- `round-001-p-screen` candidate=`left-p=100.500000; right-p=100.500000` accepted=`False` reason=`screen left score improved from inf to 0.832630; pending confirmation; screen right score improved from 1.967518 to 1.772734; pending confirmation` left_score=`0.832630` right_score=`1.772734`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/round-001-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/round-001-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/round-001-p-screen/attempt-01/board.log`
- `round-002-p-confirm` candidate=`left-p=100.500000; right-p=100.500000` accepted=`True` reason=`left score improved from inf to 1.260977; selected best candidate after paired screen+confirm at left-p=100.500000` left_score=`1.260977` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/round-002-p-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/round-002-p-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T114214Z/round-002-p-confirm/attempt-01/board.log`
