# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-23T12:07:52Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-baseline100-20260423T063744Z.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-both-d-l0_5-r0_1-working-20260423T1215Z.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-both-d-l0_5-r0_1-best-20260423T1215Z.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `inf`
- right: `1.910522`

## Rounds

- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/validation/attempt-01/board.log`
- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`inf` right_score=`1.910522`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/baseline/attempt-01/board.log`
- `baseline-screen` candidate=`current-working-params-screen` accepted=`True` reason=`baseline-screen` left_score=`0.809190` right_score=`1.662312`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/baseline-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/baseline-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/baseline-screen/attempt-01/board.log`
- `round-001-d-screen` candidate=`left-d=0.500000; right-d=0.100000` accepted=`False` reason=`screen left terminal gate failed: tail PWM jump 1036.143 > 800.000; screen right score improved from 1.662312 to 1.204665; pending confirmation` left_score=`inf` right_score=`1.204665`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/round-001-d-screen/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/round-001-d-screen/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/round-001-d-screen/attempt-01/board.log`
- `round-002-d-confirm` candidate=`right-d=0.100000` accepted=`False` reason=`confirm right score 2.001543 did not beat 1.910522 by 5%` left_score=`inf` right_score=`2.001543`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/round-002-d-confirm/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/round-002-d-confirm/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260423T120623Z/round-002-d-confirm/attempt-01/board.log`
