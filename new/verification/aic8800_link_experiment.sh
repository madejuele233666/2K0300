#!/bin/sh
set -u
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

ts="$(date +%Y%m%d-%H%M%S)"
log="/home/root/aic8800-link-experiment-${ts}.log"
cfg="/vendor/etc/firmware/aic_userconfig_8800d80.txt"
cfg_bak="${cfg}.bak.${ts}"
cfg_plus8_base="$(ls -t "${cfg}".bak.plus8base.* 2>/dev/null | sed -n '1p' || true)"

exec >>"${log}" 2>&1

apply_runtime_link_hints() {
    echo "[wifi] applying runtime link hints"
    iw dev wlan0 set power_save off 2>/dev/null || iwconfig wlan0 power off 2>/dev/null || true
    /sbin/ip link set dev wlan0 txqueuelen 100 2>/dev/null || true
    if command -v tc >/dev/null 2>&1; then
        tc qdisc replace dev wlan0 root fq_codel 2>/dev/null || true
    fi
    if [ -w /proc/sys/net/ipv4/tcp_slow_start_after_idle ]; then
        echo 0 > /proc/sys/net/ipv4/tcp_slow_start_after_idle || true
    fi
    if [ -r /proc/sys/net/ipv4/tcp_available_congestion_control ] &&
        grep -qw westwood /proc/sys/net/ipv4/tcp_available_congestion_control &&
        [ -w /proc/sys/net/ipv4/tcp_congestion_control ]; then
        echo westwood > /proc/sys/net/ipv4/tcp_congestion_control || true
    fi
    iw dev wlan0 get power_save 2>/dev/null || true
    if command -v tc >/dev/null 2>&1; then
        tc qdisc show dev wlan0 2>/dev/null || true
    fi
    if [ -r /proc/sys/net/ipv4/tcp_congestion_control ]; then
        printf "tcp_congestion_control="
        cat /proc/sys/net/ipv4/tcp_congestion_control || true
    fi
    if [ -r /proc/sys/net/ipv4/tcp_slow_start_after_idle ]; then
        printf "tcp_slow_start_after_idle="
        cat /proc/sys/net/ipv4/tcp_slow_start_after_idle || true
    fi
}

echo "[start] ${ts}"
echo "[baseline] wireless"
cat /proc/net/wireless 2>/dev/null || true
echo "[baseline] module params"
for p in ps_on dpsm uapsd_queues uapsd_timeout country_code; do
    if [ -f "/sys/module/aic8800_fdrv/parameters/${p}" ]; then
        printf "%s=" "${p}"
        cat "/sys/module/aic8800_fdrv/parameters/${p}" || true
    fi
done

if [ -f "${cfg}" ]; then
    if [ -n "${cfg_plus8_base}" ] && [ -f "${cfg_plus8_base}" ]; then
        echo "[power] using existing plus8 base=${cfg_plus8_base}"
    else
        cfg_plus8_base="${cfg}.bak.plus8base.${ts}"
        cp -a "${cfg}" "${cfg_plus8_base}" || true
        echo "[power] created plus8 base=${cfg_plus8_base}"
    fi
    cp -a "${cfg}" "${cfg_bak}" || true
    echo "[power] current backup=${cfg_bak}"
    awk -F= '
        BEGIN { OFS = "=" }
        /^lvl_.*_5g=/ { print $1, $2 + 8; next }
        /^lvl_adj_enable=/ { print "lvl_adj_enable=0"; next }
        /^lvl_adj_5g_chan_/ { print $1, 0; next }
        { print }
    ' "${cfg_plus8_base}" > /tmp/aic_userconfig_8800d80.txt &&
        install -m 644 /tmp/aic_userconfig_8800d80.txt "${cfg}" || true
    echo "[power] configured base 5g tx power +8; disabled unsupported lvl_adj path"
    grep -E '^(lvl_11a_6m_5g|lvl_11n_11ac_mcs0_5g|lvl_11ax_mcs0_5g|lvl_adj_enable|lvl_adj_5g_chan_)=' "${cfg}" || true
fi

echo "[wifi] stopping services/processes"
systemctl stop wlan0-connect.service 2>/dev/null || true
killall udhcpc 2>/dev/null || true
killall wpa_supplicant 2>/dev/null || true
sleep 1
ip link set wlan0 down 2>/dev/null || true
sleep 1

echo "[wifi] unloading modules"
rmmod aic8800_fdrv 2>/dev/null || true
sleep 1
rmmod aic8800_bsp 2>/dev/null || true
sleep 1

echo "[wifi] loading modules: ps_on=N uapsd disabled"
insmod /lib/modules/4.19.190+/aic8800_bsp.ko 2>/dev/null || modprobe aic8800_bsp 2>/dev/null || true
sleep 1
insmod /lib/modules/4.19.190+/aic8800_fdrv.ko ps_on=N uapsd_timeout=0 uapsd_queues=0 2>/dev/null || \
    modprobe aic8800_fdrv ps_on=N uapsd_timeout=0 uapsd_queues=0 2>/dev/null || true
sleep 2

echo "[wifi] module params after reload"
for p in ps_on dpsm uapsd_queues uapsd_timeout country_code; do
    if [ -f "/sys/module/aic8800_fdrv/parameters/${p}" ]; then
        printf "%s=" "${p}"
        cat "/sys/module/aic8800_fdrv/parameters/${p}" || true
    fi
done

echo "[wifi] reconnecting"
/usr/local/sbin/wlan0-connect.sh || true
sleep 5
apply_runtime_link_hints

echo "[after] wireless"
cat /proc/net/wireless 2>/dev/null || true
echo "[after] addr"
/sbin/ip -br addr show wlan0 2>/dev/null || ifconfig wlan0 2>/dev/null || true
echo "[after] route"
/sbin/ip route 2>/dev/null || route -n 2>/dev/null || true
echo "[after] ping"
ping -c 10 -W 2 192.168.137.1 || true
echo "[done] ${ts}"
