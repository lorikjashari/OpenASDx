# Settings shared by the aws/ scripts; they source this file.
# Frankfurt only: the team keeps its data in the EU, and F2 exists there (in eu-central-1b and 1c).

export AWS_REGION=eu-central-1
AZ=eu-central-1b
# The account's default VPC in eu-central-1. The IAM policy lets us launch only there.
VPC=vpc-43d0e828
# AWS's FPGA Developer AMI (Ubuntu) 1.19.2 in eu-central-1, which FireSim uses, for the manager and the F2 alike.
AMI=ami-092ebd0b9baef5842
# The IAM policy allows only launches with this tag on the instance, volume and network interface.
TAG_KEY=Project
TAG_VALUE=open-gpu-swiss-ai
SG_NAME=${OPENASDX_SG:-ogsa-firesim}
KEY_NAME=${OPENASDX_KEY:-ogsa-firesim}
KEY_FILE=${OPENASDX_KEY_FILE:-$HOME/.ssh/$KEY_NAME.pem}

TAG="{Key=$TAG_KEY,Value=$TAG_VALUE}"

# ensure_key: create the key pair if it is missing, and check that its private key is here.
ensure_key() {
  if ! aws ec2 describe-key-pairs --key-names "$KEY_NAME" >/dev/null 2>&1; then
    [[ ! -e "$KEY_FILE" ]] || { echo "$KEY_FILE exists but key pair $KEY_NAME does not" >&2; return 1; }
    aws ec2 create-key-pair --key-name "$KEY_NAME" --key-type ed25519 \
      --tag-specifications "ResourceType=key-pair,Tags=[$TAG]" \
      --query KeyMaterial --output text > "$KEY_FILE"
    chmod 600 "$KEY_FILE"
    echo "created key pair $KEY_NAME, private key in $KEY_FILE" >&2
  fi
  [[ -e "$KEY_FILE" ]] || { echo "key pair $KEY_NAME exists, but $KEY_FILE is missing: ask whoever holds it" >&2; return 1; }
}

# launch TYPE NAME DISK_GB: launch a tagged instance in $AZ with sshd on 22 and 443, wait until it
# runs, and print "ID PUBLIC_IP PRIVATE_IP".
launch() {
  set -e  # also inside $(launch ...), where bash 3.2 does not inherit it
  local type=$1 name=$2 disk=$3 here sg subnet id
  here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
  ensure_key
  sg=$("$here/security-group.sh")
  subnet=$(aws ec2 describe-subnets --filters Name=vpc-id,Values=$VPC Name=availability-zone,Values=$AZ \
    --query 'Subnets[0].SubnetId' --output text)
  id=$(aws ec2 run-instances --image-id $AMI --instance-type "$type" \
    --key-name "$KEY_NAME" --subnet-id "$subnet" --security-group-ids "$sg" --associate-public-ip-address \
    --user-data "file://$here/userdata-ssh443.sh" \
    --block-device-mappings "DeviceName=/dev/sda1,Ebs={VolumeSize=$disk,VolumeType=gp3,DeleteOnTermination=true}" \
                            'DeviceName=/dev/sdm,NoDevice=""' \
    --tag-specifications "ResourceType=instance,Tags=[$TAG,{Key=Name,Value=$name}]" \
                         "ResourceType=volume,Tags=[$TAG]" "ResourceType=network-interface,Tags=[$TAG]" \
    --query 'Instances[0].InstanceId' --output text)
  echo "launched $name ($type, $id), waiting for it to run" >&2
  aws ec2 wait instance-running --instance-ids "$id"
  aws ec2 describe-instances --instance-ids "$id" \
    --query 'Reservations[0].Instances[0].[InstanceId,PublicIpAddress,PrivateIpAddress]' --output text
}

# Our instances are the tagged ones named ogsa-firesim-*. Others in the account carry the project
# tag too (the admin's tests), so never select by the tag alone.
NAME_PREFIX=ogsa-firesim-

# instances [STATE...]: print "ID NAME STATE TYPE PUBLIC_IP PRIVATE_IP LAUNCH_TIME" for our instances.
instances() {
  local states=${*:-pending running stopping stopped}
  aws ec2 describe-instances \
    --filters "Name=tag:$TAG_KEY,Values=$TAG_VALUE" "Name=tag:Name,Values=${NAME_PREFIX}*" \
              "Name=instance-state-name,Values=${states// /,}" \
    --query 'Reservations[].Instances[].[InstanceId,Tags[?Key==`Name`]|[0].Value,State.Name,InstanceType,PublicIpAddress,PrivateIpAddress,LaunchTime]' \
    --output text
}
