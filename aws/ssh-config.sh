#!/usr/bin/env bash
# Print ~/.ssh/config entries for our running instances, on port 443 (some networks block 22).
# The manager reaches the F2 by private IP instead, which does not change.
set -euo pipefail
source "$(dirname "$0")/common.sh"
instances running | sort -k2 | while read -r id name state type ip private_ip _; do
  printf 'Host %s\n  HostName %s\n  Port 443\n  User ubuntu\n  IdentityFile %s\n\n' "$name" "$ip" "$KEY_FILE"
done
