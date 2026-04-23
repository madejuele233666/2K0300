# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T03:57:26Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-finescan-20260423-left140-right90.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-finescan-working-20260423-left140-right90.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-finescan-best-20260423-left140-right90.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `1.349615`
- right: `inf`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`1.575325` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/baseline-screen/attempt-01/board.log`
- `round-001-p-screen` candidate=`left-p=136.000000; right-p=86.000000` accepted=`False` reason=`screen left score 1.670057 did not beat 1.575325 by 5%; screen right terminal gate failed: tail error 21.143 > 20.000; tail PWM jump 1023.571 > 800.000` left_score=`1.670057` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-001-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-001-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-001-p-screen/attempt-01/board.log`
- `round-002-p-screen` candidate=`left-p=137.000000; right-p=87.000000` accepted=`False` reason=`screen left terminal gate failed: terminal gate failed; screen right terminal gate failed: tail error 20.254 > 20.000; tail PWM jump 813.381 > 800.000` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-002-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-002-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-002-p-screen/attempt-01/board.log`
- `round-003-p-screen` candidate=`left-p=138.000000; right-p=88.000000` accepted=`False` reason=`screen left terminal gate failed: terminal gate failed; screen right terminal gate failed: tail PWM jump 823.857 > 800.000` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-003-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-003-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-003-p-screen/attempt-01/board.log`
- `round-004-p-screen` candidate=`left-p=139.000000; right-p=89.000000` accepted=`False` reason=`screen left score improved from 1.575325 to 1.355547; pending confirmation; screen right score improved from inf to 1.947691; pending confirmation` left_score=`1.355547` right_score=`1.947691`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-004-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-004-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-004-p-screen/attempt-01/board.log`
- `round-005-p-screen` candidate=`left-p=141.000000; right-p=91.000000` accepted=`False` reason=`screen left score 1.715185 did not beat 1.575325 by 5%; screen right terminal gate failed: tail error 20.365 > 20.000` left_score=`1.715185` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-005-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-005-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-005-p-screen/attempt-01/board.log`
- `round-006-p-screen` candidate=`left-p=142.000000; right-p=92.000000` accepted=`False` reason=`screen left score 1.591108 did not beat 1.575325 by 5%; screen right terminal gate failed: terminal gate failed` left_score=`1.591108` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-006-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-006-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-006-p-screen/attempt-01/board.log`
- `round-007-p-screen` candidate=`left-p=143.000000; right-p=93.000000` accepted=`False` reason=`screen left terminal gate failed: tail PWM jump 981.143 > 800.000; screen right terminal gate failed: terminal gate failed` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-007-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-007-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-007-p-screen/attempt-01/board.log`
- `round-008-p-screen` candidate=`left-p=144.000000; right-p=94.000000` accepted=`False` reason=`screen left terminal gate failed: terminal gate failed; screen right terminal gate failed: terminal gate failed` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-008-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-008-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-008-p-screen/attempt-01/board.log`
- `round-009-p-confirm` candidate=`left-p=139.000000; right-p=89.000000` accepted=`True` reason=`left score improved from inf to 1.349615; selected best candidate after paired screen+confirm at left-p=139.000000` left_score=`1.349615` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-009-p-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-009-p-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T035347Z/round-009-p-confirm/attempt-01/board.log`
