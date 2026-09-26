#!/usr/bin/env bash
# Create the project security group if it is missing, and let the caller's public IP in on SSH
# (ports 22 and 443). Members of the group can also reach each other on 22 (manager to F2).
# Prints the group id. Run it again from a new network to add that IP too.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/common.sh"

sg=$(aws ec2 describe-security-groups \
  --filters Name=vpc-id,Values=$VPC Name=group-name,Values=$SG_NAME \
  --query 'SecurityGroups[0].GroupId' --output text)
if [[ "$sg" == None ]]; then
  sg=$(aws ec2 create-security-group --group-name "$SG_NAME" --vpc-id $VPC \
    --description "OpenASDx FireSim: SSH on 22 and 443 from team IPs, SSH between members" \
    --tag-specifications "ResourceType=security-group,Tags=[$TAG,{Key=Name,Value=$SG_NAME}]" \
    --query GroupId --output text)
  aws ec2 authorize-security-group-ingress --group-id "$sg" --output text >/dev/null \
    --ip-permissions "IpProtocol=tcp,FromPort=22,ToPort=22,UserIdGroupPairs=[{GroupId=$sg,Description=between members}]"
  echo "created security group $SG_NAME ($sg)" >&2
fi

ip=$(curl -fsS https://checkip.amazonaws.com)
for port in 22 443; do
  out=$(aws ec2 authorize-security-group-ingress --group-id "$sg" --output text 2>&1 \
    --ip-permissions "IpProtocol=tcp,FromPort=$port,ToPort=$port,IpRanges=[{CidrIp=$ip/32,Description=ssh}]") ||
    [[ "$out" == *InvalidPermission.Duplicate* ]] || { echo "$out" >&2; exit 1; }
done
echo "$ip may reach $SG_NAME on ports 22 and 443" >&2
echo "$sg"
