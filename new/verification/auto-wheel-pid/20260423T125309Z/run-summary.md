# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T12:55:28Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-pcenter-l84-r96-iformal-20260423T1255Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-pmixscan-working-20260423T1255Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-pmixscan-best-20260423T1255Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `0.975240`
- right: `1.376077`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`0.975240` right_score=`1.376077`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/baseline-screen/attempt-01/board.log`
- `round-001-p-screen` candidate=`left-p=68.000000; right-p=94.000000` accepted=`False` reason=`screen left score improved from inf to 1.056608; pending confirmation; screen right score improved from inf to 1.403407; pending confirmation` left_score=`1.056608` right_score=`1.403407`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-001-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-001-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-001-p-screen/attempt-01/board.log`
- `round-002-p-screen` candidate=`left-p=72.000000; right-p=95.000000` accepted=`False` reason=`screen left score improved from inf to 0.749763; pending confirmation; screen right terminal gate failed: tail PWM jump 900.333 > 800.000` left_score=`0.749763` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-002-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-002-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-002-p-screen/attempt-01/board.log`
- `round-003-p-screen` candidate=`left-p=76.000000; right-p=97.000000` accepted=`False` reason=`screen left terminal gate failed: terminal gate failed; screen right terminal gate failed: terminal gate failed` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-003-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-003-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-003-p-screen/attempt-01/board.log`
- `round-004-p-screen` candidate=`left-p=80.000000; right-p=98.000000` accepted=`False` reason=`screen left terminal gate failed: terminal gate failed; screen right terminal gate failed: terminal gate failed` left_score=`inf` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-004-p-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-004-p-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-004-p-screen/attempt-01/board.log`
- `round-005-p-confirm` candidate=`left-p=72.000000; right-p=94.000000` accepted=`False` reason=`confirm left score 1.140684 did not beat 0.975240 by 5%; confirm right terminal gate failed: tail PWM jump 831.952 > 800.000` left_score=`1.140684` right_score=`inf`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-005-p-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-005-p-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T125309Z/round-005-p-confirm/attempt-01/board.log`
