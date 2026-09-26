# Settings shared by the aws/ scripts; they source this file.
# Frankfurt only: the team keeps its data in the EU, and F2 exists there (in eu-central-1b and 1c).

export AWS_REGION=eu-central-1
AZ=eu-central-1b
# The account's default VPC in eu-central-1. The IAM policy lets us launch only there.
VPC=vpc-43d0e828
# FireSim's Ubuntu 24.04 F2 AMI (FireSim 1.19.2) in eu-central-1, for the manager and the F2 alike.
AMI=ami-092ebd0b9baef5842
# The IAM policy allows only launches with this tag on the instance, volume and network interface.
TAG_KEY=Project
TAG_VALUE=open-gpu-swiss-ai
SG_NAME=${OPENASDX_SG:-ogsa-firesim}
KEY_NAME=${OPENASDX_KEY:-ogsa-firesim}
KEY_FILE=${OPENASDX_KEY_FILE:-$HOME/.ssh/$KEY_NAME.pem}

TAG="{Key=$TAG_KEY,Value=$TAG_VALUE}"
