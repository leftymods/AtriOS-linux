mkdir -p /cache/recovery
echo "boot-recovery\n--update_package=$1" > /cache/recovery/command

# FIXME: restore graceful reboot -- see https://st.yandex-team.ru/QUASAR-6570
# reboot recovery
setprop sys.powerctl reboot,recovery
