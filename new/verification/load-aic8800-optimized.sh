#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

if ! lsmod | grep -q "^aic8800_bsp "; then
    modprobe aic8800_bsp 2>/dev/null || insmod /lib/modules/4.19.190+/aic8800_bsp.ko
fi

sleep 1

if ! lsmod | grep -q "^aic8800_fdrv "; then
    insmod /lib/modules/4.19.190+/aic8800_fdrv.ko ps_on=0 uapsd_timeout=0 uapsd_queues=0 2>/dev/null || \
        modprobe aic8800_fdrv ps_on=0 uapsd_timeout=0 uapsd_queues=0
fi
