# Auto Wheel PID Tuning Summary

- generated_at: `2026-04-22T15:16:54Z`
- params_in: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-manual-seed-20260422.json`
- params_working: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-working.json`
- params_best: `/home/madejuele/projects/2K0300/new/verification/params/auto-wheel-pid-best.json`
- promoted: `False`
- listener_ip: `10.100.170.115`

## Best Scores

- left: `45.760765`
- right: `61.108075`

## Rounds

- `baseline` candidate=`current-working-params` accepted=`True` reason=`baseline` left_score=`147.925329` right_score=`61.108075`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/baseline/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/baseline/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/baseline/attempt-01/board.log`
- `round-001-left-p` candidate=`left-p=45.000000` accepted=`False` reason=`left score improved to 65.725090, but left-p=45.000000 was not the best candidate in the dimension batch` left_score=`65.725090` right_score=`62.170129`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-001-left-p/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-001-left-p/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-001-left-p/attempt-01/board.log`
- `round-002-left-p` candidate=`left-p=67.500000` accepted=`True` reason=`left score improved from 147.925329 to 45.760765; selected best candidate from bidirectional search at left-p=67.500000` left_score=`45.760765` right_score=`72.621024`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-002-left-p/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-002-left-p/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-002-left-p/attempt-01/board.log`
- `round-003-left-p` candidate=`left-p=112.500000` accepted=`False` reason=`left score 143.335252 did not beat 147.925329 by 5%` left_score=`143.335252` right_score=`90.196123`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-003-left-p/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-003-left-p/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-003-left-p/attempt-01/board.log`
- `round-004-left-p` candidate=`left-p=135.000000` accepted=`False` reason=`left score 181.670839 did not beat 147.925329 by 5%` left_score=`181.670839` right_score=`62.417196`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-004-left-p/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-004-left-p/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-004-left-p/attempt-01/board.log`
- `round-005-right-p` candidate=`right-p=35.156250` accepted=`False` reason=`right score 90.348730 did not beat 61.108075 by 5%` left_score=`159.070985` right_score=`90.348730`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-005-right-p/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-005-right-p/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-005-right-p/attempt-01/board.log`
- `round-006-right-p` candidate=`right-p=52.734375` accepted=`False` reason=`right score 67.894882 did not beat 61.108075 by 5%` left_score=`106.517918` right_score=`67.894882`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-006-right-p/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-006-right-p/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-006-right-p/attempt-01/board.log`
- `round-007-right-p` candidate=`right-p=87.890625` accepted=`False` reason=`right score 87.548471 did not beat 61.108075 by 5%` left_score=`116.024501` right_score=`87.548471`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-007-right-p/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-007-right-p/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-007-right-p/attempt-01/board.log`
- `round-008-right-p` candidate=`right-p=105.468750` accepted=`False` reason=`right score 143.882960 did not beat 61.108075 by 5%` left_score=`96.581425` right_score=`143.882960`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-008-right-p/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-008-right-p/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-008-right-p/attempt-01/board.log`
- `round-009-left-i` candidate=`left-i=0.000000` accepted=`False` reason=`left score 126.083582 did not beat 45.760765 by 5%` left_score=`126.083582` right_score=`60.497643`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-009-left-i/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-009-left-i/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-009-left-i/attempt-01/board.log`
- `round-010-left-i` candidate=`left-i=0.062500` accepted=`False` reason=`left score 102.440521 did not beat 45.760765 by 5%` left_score=`102.440521` right_score=`87.003738`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-010-left-i/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-010-left-i/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-010-left-i/attempt-01/board.log`
- `round-011-left-i` candidate=`left-i=0.125000` accepted=`False` reason=`left score 108.019659 did not beat 45.760765 by 5%` left_score=`108.019659` right_score=`47.306183`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-011-left-i/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-011-left-i/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-011-left-i/attempt-01/board.log`
- `round-012-left-i` candidate=`left-i=0.500000` accepted=`False` reason=`left score 87.421845 did not beat 45.760765 by 5%` left_score=`87.421845` right_score=`64.914637`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-012-left-i/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-012-left-i/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-012-left-i/attempt-01/board.log`
- `round-013-left-i` candidate=`left-i=1.000000` accepted=`False` reason=`left score 137.207715 did not beat 45.760765 by 5%` left_score=`137.207715` right_score=`68.595155`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-013-left-i/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-013-left-i/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-013-left-i/attempt-01/board.log`
- `round-014-right-i` candidate=`right-i=0.000000` accepted=`False` reason=`right score 69.356520 did not beat 61.108075 by 5%` left_score=`72.163083` right_score=`69.356520`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-014-right-i/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-014-right-i/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-014-right-i/attempt-01/board.log`
- `round-015-right-i` candidate=`right-i=0.125000` accepted=`False` reason=`right score 73.038979 did not beat 61.108075 by 5%` left_score=`142.623641` right_score=`73.038979`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-015-right-i/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-015-right-i/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-015-right-i/attempt-01/board.log`
- `round-016-right-i` candidate=`right-i=0.250000` accepted=`False` reason=`right score 62.614397 did not beat 61.108075 by 5%` left_score=`66.095519` right_score=`62.614397`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-016-right-i/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-016-right-i/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-016-right-i/attempt-01/board.log`
- `round-017-right-i` candidate=`right-i=1.000000` accepted=`False` reason=`right score 99.669973 did not beat 61.108075 by 5%` left_score=`115.462850` right_score=`99.669973`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-017-right-i/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-017-right-i/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-017-right-i/attempt-01/board.log`
- `round-018-right-i` candidate=`right-i=2.000000` accepted=`False` reason=`right score 90.218308 did not beat 61.108075 by 5%` left_score=`123.492088` right_score=`90.218308`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-018-right-i/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-018-right-i/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-018-right-i/attempt-01/board.log`
- `round-019-left-d` candidate=`left-d=0.000000` accepted=`False` reason=`left score 84.303757 did not beat 45.760765 by 5%` left_score=`84.303757` right_score=`58.115075`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-019-left-d/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-019-left-d/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-019-left-d/attempt-01/board.log`
- `round-020-left-d` candidate=`left-d=0.125000` accepted=`False` reason=`left score 99.173424 did not beat 45.760765 by 5%` left_score=`99.173424` right_score=`90.151254`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-020-left-d/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-020-left-d/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-020-left-d/attempt-01/board.log`
- `round-021-left-d` candidate=`left-d=0.250000` accepted=`False` reason=`left score 112.999594 did not beat 45.760765 by 5%` left_score=`112.999594` right_score=`70.792319`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-021-left-d/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-021-left-d/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-021-left-d/attempt-01/board.log`
- `round-022-left-d` candidate=`left-d=1.000000` accepted=`False` reason=`left score 96.000171 did not beat 45.760765 by 5%` left_score=`96.000171` right_score=`118.790673`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-022-left-d/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-022-left-d/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-022-left-d/attempt-01/board.log`
- `round-023-left-d` candidate=`left-d=2.000000` accepted=`False` reason=`left score 96.085402 did not beat 45.760765 by 5%` left_score=`96.085402` right_score=`105.255418`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-023-left-d/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-023-left-d/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-023-left-d/attempt-01/board.log`
- `round-024-right-d` candidate=`right-d=0.000000` accepted=`False` reason=`right score 73.186004 did not beat 61.108075 by 5%` left_score=`131.761726` right_score=`73.186004`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-024-right-d/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-024-right-d/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-024-right-d/attempt-01/board.log`
- `round-025-right-d` candidate=`right-d=0.125000` accepted=`False` reason=`right score 84.498945 did not beat 61.108075 by 5%` left_score=`76.655831` right_score=`84.498945`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-025-right-d/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-025-right-d/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-025-right-d/attempt-01/board.log`
- `round-026-right-d` candidate=`right-d=0.250000` accepted=`False` reason=`right score 113.043347 did not beat 61.108075 by 5%` left_score=`132.661502` right_score=`113.043347`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-026-right-d/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-026-right-d/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-026-right-d/attempt-01/board.log`
- `round-027-right-d` candidate=`right-d=1.000000` accepted=`False` reason=`right score 95.169070 did not beat 61.108075 by 5%` left_score=`94.908179` right_score=`95.169070`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-027-right-d/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-027-right-d/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-027-right-d/attempt-01/board.log`
- `round-028-right-d` candidate=`right-d=2.000000` accepted=`False` reason=`right score 81.160523 did not beat 61.108075 by 5%` left_score=`88.922972` right_score=`81.160523`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-028-right-d/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-028-right-d/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/round-028-right-d/attempt-01/board.log`
- `validation` candidate=`protocol-validation` accepted=`True` reason=`validated` left_score=`0.000000` right_score=`0.000000`
  csv=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/validation/attempt-01/telemetry.csv`
  host_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/validation/attempt-01/host.log`
  board_log=`/home/madejuele/projects/2K0300/new/verification/auto-wheel-pid/20260422T135601Z/validation/attempt-01/board.log`
