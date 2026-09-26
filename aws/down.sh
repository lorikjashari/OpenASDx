#!/usr/bin/env bash
# Stop our FireSim instances (all of them, or those named on the command line, e.g. ogsa-firesim-f2).
# Stopped instances keep their disks and private IPs, and cost only the disk. The public IP changes.
set -euo pipefail
source "$(dirname "$0")/common.sh"
ids=$(instances pending running | awk -v names=" $* " 'names == "  " || index(names, " " $2 " ") {print $1}')
[[ -n "$ids" ]] || { echo "nothing running" >&2; exit 0; }
aws ec2 stop-instances --instance-ids $ids --query 'StoppingInstances[].[InstanceId,CurrentState.Name]' --output text
