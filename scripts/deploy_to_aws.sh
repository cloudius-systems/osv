#!/bin/bash
#
#This script deploys latest built OSv image to AWS as an EC2 instance
#
#It does so it two steps:
#1. Uploads the latest raw image under build/last/usr.raw as an EC2 instance
#   snapshot using the flexible snapshot proxy and creates new AMI out of it.
#2. Creates new single EC2 instance of type specified by the optional INSTANCE_TYPE
#   parameter (t2.* - Xen, t3.* and other - KVM) using newly created AMI

VPC_ID=$1
SUBNET_ID=$2
INSTANCE_NAME=$3
INSTANCE_TYPE=${4:-t3.nano}

if [[ "$VPC_ID" == "" || "$SUBNET_ID" == "" || "$INSTANCE_NAME" == "" ]]; then
  echo "Usage: ./scripts/deploy_to_aws.sh <VPC_ID> <SUBNET_ID> <INSTANCE_NAME> <INSTANCE_TYPE> (optional)"
  exit -1
fi

if [[ ! "$INSTANCE_NAME" =~ ^[a-zA-Z][-a-zA-Z0-9]*$ ]]; then
  echo "Invalid INSTANCE_NAME! Should match regex [a-zA-Z][-a-zA-Z0-9]*"
  exit -1
fi

qemu-img convert -O raw build/last/usr.img build/last/usr.raw
echo "Converted latest image to raw format; $(readlink -f build/last/usr.raw)"

SNAPSHOT_PROXY_DIR=${SNAPSHOT_PROXY_DIR:-~/projects/flexible-snapshot-proxy}
if [[ ! -d $SNAPSHOT_PROXY_DIR ]]; then
  echo "!!! Failed to find the flexible snapshot proxy at $SNAPSHOT_PROXY_DIR"
  echo "    Please download it from https://github.com/awslabs/flexible-snapshot-proxy"
  echo "    and pass its location as env variable SNAPSHOT_PROXY_DIR"
  exit -1
fi

snapshot_id=$(python3 $SNAPSHOT_PROXY_DIR/src/main.py upload build/last/usr.raw | tail -n 1)
echo "Created snapshot: $snapshot_id"

ami_id=$(./scripts/ec2-make-ami.py -n "$INSTANCE_NAME" -s "$snapshot_id" | grep '^ami' | tail -n 1)
echo "Created AMI: $ami_id"

cat scripts/aws/instance-parameters.json | sed -e "s/INSTANCE_NAME/$INSTANCE_NAME/" | \
  sed -e "s/VPC_ID/$VPC_ID/" | sed -e "s/SUBNET_ID/$SUBNET_ID/" | \
  sed -e "s/INSTANCE_TYPE/$INSTANCE_TYPE/" | sed -e "s/AMI_ID/$ami_id/" > /tmp/instance-parameters.json

echo "Deploying stack named: $INSTANCE_NAME using cloudformation ./scripts/aws/instance.yaml with parameters file:"
cat /tmp/instance-parameters.json

STACK_ID=$(aws cloudformation create-stack \
 --stack-name $INSTANCE_NAME \
 --template-body file://./scripts/aws/instance.yaml \
 --capabilities CAPABILITY_IAM \
 --parameters file:///tmp/instance-parameters.json | grep "StackId" | grep -oP 'arn[^"]*')

echo "Initalized deployment of stack $INSTANCE_NAME with id:$STACK_ID!"
echo "Waiting for it to complete ..."
while [[ "$STACK_STATUS" == "" ]]; do
  sleep 5
  STACK_STATUS=$(aws cloudformation describe-stacks --stack-name "$STACK_ID" --query "Stacks[0].StackStatus" | grep "COMPLETE\|FAILED")
done

if [[ "$STACK_STATUS" == '"CREATE_COMPLETE"' ]]; then
  INSTANCE_DNS=$(aws cloudformation describe-stacks --stack-name "$STACK_ID" --query "Stacks[0].Outputs[1].OutputValue")
  echo "New EC2 instance is ready to test: [$INSTANCE_DNS]!"
else
  echo "Attempt to create new stack: $STACK_ID failed with status: $STACK_STATUS"
fi

#To clean
#aws ec2 deregister-image --image-id <id>
#aws ec2 delete-snapshot --snapshot-id <id>
