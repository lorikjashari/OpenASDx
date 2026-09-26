#!/usr/bin/env bash
# Launch a FireSim manager: a c5.4xlarge with FireSim's F2 AMI and a 300 GB disk, in Frankfurt,
# with sshd on 22 and 443. It has no FPGA; it builds software and drives the F2.
# Usage: aws/launch-manager.sh [name]     (default name: ogsa-firesim-manager)
# Needs an AWS CLI profile for the project (AWS_PROFILE). It costs money until you stop it.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/common.sh"
name=${1:-ogsa-firesim-manager}

if ! aws ec2 describe-key-pairs --key-names "$KEY_NAME" >/dev/null 2>&1; then
  [[ ! -e "$KEY_FILE" ]] || { echo "$KEY_FILE exists but key pair $KEY_NAME does not" >&2; exit 1; }
  aws ec2 create-key-pair --key-name "$KEY_NAME" --key-type ed25519 \
    --tag-specifications "ResourceType=key-pair,Tags=[$TAG]" \
    --query KeyMaterial --output text > "$KEY_FILE"
  chmod 600 "$KEY_FILE"
  echo "created key pair $KEY_NAME, private key in $KEY_FILE" >&2
fi
[[ -e "$KEY_FILE" ]] || { echo "key pair $KEY_NAME exists, but $KEY_FILE is missing: ask whoever holds it" >&2; exit 1; }

sg=$("$here/security-group.sh")
subnet=$(aws ec2 describe-subnets --filters Name=vpc-id,Values=$VPC Name=availability-zone,Values=$AZ \
  --query 'Subnets[0].SubnetId' --output text)

id=$(aws ec2 run-instances --image-id $AMI --instance-type c5.4xlarge \
  --key-name "$KEY_NAME" --subnet-id "$subnet" --security-group-ids "$sg" --associate-public-ip-address \
  --user-data "file://$here/userdata-ssh443.sh" \
  --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=300,VolumeType=gp3,DeleteOnTermination=true}' \
                          'DeviceName=/dev/sdm,NoDevice=""' \
  --tag-specifications "ResourceType=instance,Tags=[$TAG,{Key=Name,Value=$name}]" \
                       "ResourceType=volume,Tags=[$TAG]" "ResourceType=network-interface,Tags=[$TAG]" \
  --query 'Instances[0].InstanceId' --output text)
echo "launched $name ($id), waiting for it to run" >&2
aws ec2 wait instance-running --instance-ids "$id"
ip=$(aws ec2 describe-instances --instance-ids "$id" \
  --query 'Reservations[0].Instances[0].PublicIpAddress' --output text)

cat <<EOF
$name: $id at $ip. Add this to ~/.ssh/config:

Host $name
  HostName $ip
  Port 443
  User ubuntu
  IdentityFile $KEY_FILE

sshd comes up on 443 about a minute after boot. Then see aws/README.md, "Set up the manager".
EOF
