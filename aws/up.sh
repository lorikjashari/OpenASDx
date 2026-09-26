#!/usr/bin/env bash
# Start our stopped FireSim instances (all of them, or those named on the command line), wait until
# they run, and print fresh ~/.ssh/config entries (public IPs change on every start).
set -euo pipefail
here=$(dirname "$0")
source "$here/common.sh"
ids=$(instances stopped | awk -v names=" $* " 'names == "  " || index(names, " " $2 " ") {print $1}')
if [[ -n "$ids" ]]; then
  aws ec2 start-instances --instance-ids $ids --output text >/dev/null
  aws ec2 wait instance-running --instance-ids $ids
fi
"$here/ssh-config.sh"
