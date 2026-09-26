#!/usr/bin/env bash
# Launch a FireSim run host: an f2.6xlarge (one FPGA) with FireSim's F2 AMI, in Frankfurt.
# FireSim can't launch it under our IAM policy, so we launch it here and give FireSim its private
# IP as an externally provisioned host.
# Usage: aws/launch-run-host.sh [name]    (default name: ogsa-firesim-f2)
# An f2.6xlarge is expensive: stop it as soon as the runs are done.
set -euo pipefail
source "$(dirname "$0")/common.sh"
name=${1:-ogsa-firesim-f2}

out=$(launch f2.6xlarge "$name" 200)
read -r id ip private_ip <<< "$out"
cat <<END
$name: $id, public $ip, private $private_ip.
The manager reaches it at $private_ip on port 22; see aws/README.md, "Use the F2".
Stop it with: aws ec2 stop-instances --instance-ids $id
END
