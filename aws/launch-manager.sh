#!/usr/bin/env bash
# Launch a FireSim manager: a c5.4xlarge with FireSim's F2 AMI and a 300 GB disk, in Frankfurt,
# with sshd on 22 and 443. It has no FPGA; it builds software and drives the F2.
# Usage: aws/launch-manager.sh [name]     (default name: ogsa-firesim-manager)
# Needs an AWS CLI profile for the project (AWS_PROFILE). It costs money until you stop it.
set -euo pipefail
source "$(dirname "$0")/common.sh"
name=${1:-ogsa-firesim-manager}

out=$(launch c5.4xlarge "$name" 300)
read -r id ip _ <<< "$out"
cat <<END
$name: $id at $ip. Add this to ~/.ssh/config:

Host $name
  HostName $ip
  Port 443
  User ubuntu
  IdentityFile $KEY_FILE

sshd comes up on 443 about a minute after boot. Then see aws/README.md, "Set up the manager".
END
