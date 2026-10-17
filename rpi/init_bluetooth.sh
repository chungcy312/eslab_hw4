#!/usr/bin/env bash
set -euo pipefail

# Bring up BlueZ and the local adapter after boot (or whenever the Pi needs it).
if [[ "$(uname -s)" != "Linux" ]]; then
  printf 'Bluetooth initialization is intended for Raspberry Pi OS/Linux.\n' >&2
  exit 1
fi

for tool in systemctl bluetoothctl; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'Required command not found: %s (install BlueZ first).\n' "$tool" >&2
    exit 1
  fi
done

if command -v rfkill >/dev/null 2>&1; then
  sudo rfkill unblock bluetooth
fi

# Enable at boot as well as starting it now. This makes manual reruns safe.
sudo systemctl enable --now bluetooth.service

# Ask BlueZ to power on its default adapter. It may take a few seconds to appear.
bluetoothctl power on
for attempt in {1..10}; do
  if bluetoothctl show 2>/dev/null | grep -qE '^[[:space:]]*Powered:[[:space:]]+yes'; then
    printf 'Bluetooth is powered on; bluetooth.service is enabled at boot.\n'
    exit 0
  fi
  sleep 1
done

printf 'Bluetooth service is running, but no powered adapter was found.\n' >&2
printf 'Check that the Pi has a Bluetooth adapter and that it is not blocked.\n' >&2
exit 1
