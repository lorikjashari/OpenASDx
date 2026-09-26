#!/bin/sh
# Flash the GEMM customer logic onto one FPGA of this F2 instance.
# Run this on the instance, after the AFI state is "available".
set -eu
cd "$(dirname "$0")"

if [ -f afi.env ]; then
  # shellcheck disable=SC1091
  . ./afi.env
fi

SLOT="${SLOT:-0}"
AGFI_ID="${AGFI_ID:-}"

if [ -z "$AGFI_ID" ] || [ "$AGFI_ID" = "agfi-REPLACE_ME" ]; then
  echo "Set AGFI_ID in llm/fpga/f2/afi.env (see afi.env.example)." >&2
  exit 1
fi

echo "FPGA slots:"
sudo fpga-describe-local-image-slots
echo "Clearing slot $SLOT"
sudo fpga-clear-local-image -S "$SLOT"
echo "Loading $AGFI_ID"
sudo fpga-load-local-image -S "$SLOT" -I "$AGFI_ID" -H
sudo fpga-describe-local-image -S "$SLOT" -R -H
echo "Loaded. Point the runtime at the slot BAR, for example:"
echo "  export F2_BAR=/sys/bus/pci/devices/0000:00:1e.0/resource0"
echo "  ./llm-f2"
