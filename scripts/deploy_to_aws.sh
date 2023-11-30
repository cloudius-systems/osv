#!/bin/bash
#

NAME=$1

qemu-img convert -O raw build/release/usr.img build/release/usr.raw
echo "Converted to raw image"

snapshot_id=$(python3 ~/projects/flexible-snapshot-proxy/src/main.py upload build/release/usr.raw | tail -n 1)
echo "Created snapshot: $snapshot_id"

ami_id=$(./scripts/ec2-make-ami.py -n "$NAME" -s "$snapshot_id" | grep '^ami' | tail -n 1)
echo "Created AMI: $ami_id"

cat scripts/aws/instance-parameters.json | sed -e "s/INSTANCE_NAME/$NAME/" | sed -e "s/AMI_ID/$ami_id/" > /tmp/instance-parameters.json
aws cloudformation create-stack \
 --stack-name $NAME \
 --template-body file://./scripts/aws/instance.yaml \
 --capabilities CAPABILITY_IAM \
 --parameters file:///tmp/instance-parameters.json

#To clean
#aws ec2 deregister-image --image-id <id>
#aws ec2 delete-snapshot --snapshot-id <id>
