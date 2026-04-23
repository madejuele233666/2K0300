# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T09:29:23Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-left-window-94-96-anchor-right100-20260423T070220Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-left-window-working-20260423T070220Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-left-window-best-20260423T070220Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `1.378117`
- right: `inf`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`1.378117` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`1.443265` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/baseline-screen/attempt-01/board.log`
- `round-001-p-screen` candidate=`left-p=94.000000` accepted=`False` reason=`screen left score 2.125993 did not beat 1.443265 by 5%` left_score=`2.125993` right_score=`1.842777`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/round-001-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/round-001-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/round-001-p-screen/attempt-01/board.log`
- `round-002-p-screen` candidate=`left-p=95.000000` accepted=`False` reason=`screen left score improved from 1.443265 to 0.713031; pending confirmation` left_score=`0.713031` right_score=`1.977782`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/round-002-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/round-002-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/round-002-p-screen/attempt-01/board.log`
- `round-003-p-confirm` candidate=`left-p=95.000000` accepted=`False` reason=`confirm left terminal gate failed: terminal gate failed` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/round-003-p-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/round-003-p-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T092728Z/round-003-p-confirm/attempt-01/board.log`
