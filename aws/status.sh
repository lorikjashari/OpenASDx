#!/usr/bin/env bash
# Show our FireSim instances: id, name, state, type, public IP, private IP, launch time.
set -euo pipefail
source "$(dirname "$0")/common.sh"
instances | sort -k2 | column -t
