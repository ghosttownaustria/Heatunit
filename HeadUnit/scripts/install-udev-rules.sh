#!/usr/bin/env bash
# Installs (or, with --uninstall, removes) the udev rule that lets the logged-in user open Android phones
# over USB. Needed once per computer; HeadUnit says "Keine Berechtigung" until then. Asks for the sudo password.
# Run it as: bash scripts/install-udev-rules.sh
set -euo pipefail

rule_name=70-headunit-android.rules
target="/etc/udev/rules.d/$rule_name"
source_rule="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../packaging/linux/$rule_name"

if [ "$(id -u)" -ne 0 ]; then
    exec sudo bash "$0" "$@"
fi

if [ "${1:-}" = "--uninstall" ]; then
    rm -f "$target"
    udevadm control --reload-rules
    echo "Regel entfernt: $target"
    exit 0
fi

if [ ! -f "$source_rule" ]; then
    echo "Regeldatei nicht gefunden: $source_rule" >&2
    exit 1
fi
install -m 0644 "$source_rule" "$target"
udevadm control --reload-rules
udevadm trigger --subsystem-match=usb
echo "Regel installiert: $target"
echo "Bitte das Handy jetzt einmal abziehen und wieder anstecken."
