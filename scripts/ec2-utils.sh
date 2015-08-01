#!/bin/sh
#
#  Copyright (C) 2013 Cloudius Systems, Ltd.
#
#  This work is open source software, licensed under the terms of the
#  BSD license as described in the LICENSE file in the top-level directory.
#

export AWS_ACCESS_KEY=$AWS_ACCESS_KEY_ID
export AWS_SECRET_KEY=$AWS_SECRET_ACCESS_KEY
export AWS_DEFAULT_REGION=us-east-1

timestamp() {
 echo `date -u +'%Y-%m-%dT%H-%M-%SZ'`
}

get_json_value() {
 python -c "import json,sys;obj=json.load(sys.stdin);print obj$*"
}

ec2_response_value() {
 local ROWID=$1
 local KEY=$2
 grep $ROWID | sed -n "s/^.*$KEY\s\+\(\S\+\).*$/\1/p"
}

get_volume_conversion_status() {
 local TASKID=$1
 ec2-describe-conversion-tasks $TASKID --region $AWS_DEFAULT_REGION | tee /dev/tty | ec2_response_value IMPORTVOLUME Status
}

get_created_volume_id() {
 local TASKID=$1
 ec2-describe-conversion-tasks $TASKID --region $AWS_DEFAULT_REGION | tee /dev/tty | ec2_response_value DISKIMAGE VolumeId
}

rename_object() {
 local OBJID=$1
 local OBJNAME=$2
 ec2-create-tags $OBJID --tag Name="$OBJNAME" --region $AWS_DEFAULT_REGION
}

tag_object() {
 local OBJID=$1
 local TAGNAME=$2
 local TAGVAL=$3
 shift 3

 ec2-create-tags $OBJID --tag $TAGNAME=$TAGVAL $* --region $AWS_DEFAULT_REGION
}

wait_import_completion() {
 local TASKID=$1
 local STATUS="unknown"

 while test x"$STATUS" != x"completed"
 do
   sleep 10
   STATUS=`get_volume_conversion_status $TASKID`
   echo Task is $STATUS
 done
}

get_instance_state() {
 local INSTANCE_ID=$1
 aws ec2 describe-instances --instance-ids $INSTANCE_ID | get_json_value '["Reservations"][0]["Instances"][0]["State"]["Name"]'
}

get_instance_volume_id() {
 local INSTANCE_ID=$1
 aws ec2 describe-instances --instance-ids $INSTANCE_ID | get_json_value '["Reservations"][0]["Instances"][0]["BlockDeviceMappings"][0]["Ebs"]["VolumeId"]'
}

get_volume_state() {
 local VOLUME_ID=$1
 aws ec2 describe-volumes --volume-ids $VOLUME_ID | get_json_value '["Volumes"][0]["State"]'
}

get_instance_id_by_name() {
 local INSTANCE_NAME=$1
 aws ec2 describe-instances --filters "Name=tag:Name,Values=$1" | get_json_value '["Reservations"][0]["Instances"][0]["InstanceId"]'
}

get_instance_private_ip() {
 local INSTANCE_ID=$1
 aws ec2 describe-instances --instance-ids $INSTANCE_ID | get_json_value '["Reservations"][0]["Instances"][0]["PrivateIpAddress"]'
}

change_instance_type() {
  local INSTANCE_ID=$1
  local INSTANCE_TYPE=$2
 aws ec2 modify-instance-attribute --instance-id $INSTANCE_ID --instance-type "{\"Value\": \"$INSTANCE_TYPE\"}"
}

get_ami_state() {
 local AMI_ID=$1
 shift

 aws ec2 describe-images --image-ids $AMI_ID $* | get_json_value '["Images"][0]["State"]'
}

detach_volume() {
 local VOLUME_ID=$1
 aws ec2 detach-volume --volume-id $VOLUME_ID --force
}

attach_volume() {
 local VOLUME_ID=$1
 local INSTANCE_ID=$2

 aws ec2 attach-volume --volume-id $VOLUME_ID --instance-id $INSTANCE_ID --device /dev/sda1
}

delete_volume() {
 local VOLUME_ID=$1
 aws ec2 delete-volume --volume-id $VOLUME_ID
}

delete_instance() {
 local INSTANCE_ID=$1
 aws ec2 terminate-instances --instance-ids $INSTANCE_ID
}

wait_for_instance_state() {
 local INSTANCE_ID=$1
 local REQUESTED_STATE=$2
 local TIMEOUT_SEC=$3

 local STATE="unknown"
 local TIME_PASSED=0
 local QUANTUM_SEC=5

 while test x"$STATE" != x"$REQUESTED_STATE"
 do
   sleep $QUANTUM_SEC
   STATE=`get_instance_state $INSTANCE_ID`
   echo Instance is $STATE

   TIME_PASSED=`expr $TIME_PASSED + $QUANTUM_SEC`

   if test x"$TIMEOUT_SEC" != x""; then
     if test $TIME_PASSED -ge $TIMEOUT_SEC; then
       return 123
     fi
   fi
 done
}

wait_for_instance_startup() {
 wait_for_instance_state $1 running $2
}

wait_for_instance_shutdown() {
 wait_for_instance_state $1 stopped
}

wait_for_volume_state() {
 local VOLUME_ID=$1
 local REQUESTED_STATE=$2

 local STATE="unknown"

 while test x"$STATE" != x"$REQUESTED_STATE"
 do
   sleep 5
   STATE=`get_volume_state $VOLUME_ID`
   echo Volume is $STATE
 done
}

wait_for_ami_ready() {
 local AMI_ID=$1
 shift

 local STATE="unknown"

 while test x"$STATE" != x"available"
 do
   sleep 5
   STATE=`get_ami_state $AMI_ID $*`
   echo AMI is $STATE
 done
}

wait_for_volume_detach() {
 wait_for_volume_state $1 available
}

wait_for_volume_attach() {
 wait_for_volume_state $1 in-use
}

start_instances() {
 ec2-start-instances $*
}
stop_instance_forcibly() {
 ec2-stop-instances $1 --force --region $AWS_DEFAULT_REGION
}

create_ami_by_instance() {
 local INSTANCE_ID=$1
 local VERSION=$2
 local DESCRIPTION="$3"

 aws ec2 create-image --instance-id $INSTANCE_ID --name OSv-$VERSION --description "$DESCRIPTION" | get_json_value '["ImageId"]'
}

copy_ami_to_region() {
 local AMI_ID=$1
 local REGION=$2

 aws ec2 --region $REGION copy-image --source-region $AWS_DEFAULT_REGION --source-image-id $AMI_ID | get_json_value '["ImageId"]'
}

make_ami_public() {
 local AMI_ID=$1
 shift

 ec2-modify-image-attribute $AMI_ID --launch-permission --add all $*
}

make_ami_private() {
 local AMI_ID=$1
 shift

 ec2-modify-image-attribute $AMI_ID --launch-permission --remove all $*
}

list_regions() {

 if test x"$REGIONS_LIST" = x""; then
     ec2-describe-regions | ec2_response_value REGION REGION
 else
     for region in $REGIONS_LIST; do echo $region; done
 fi

}

get_own_ami_info() {
 local AMI_ID=$1
 shift

 ec2-describe-images $* | grep $AMI_ID
}

get_public_ami_info() {
 local AMI_ID=$1
 shift

 ec2-describe-images -x all $* | grep $AMI_ID
}

# This function implements work-around for AMIs copying problem described here:
# https://forums.aws.amazon.com/thread.jspa?messageID=454676&#454676
make_replica_public() {
  local AMI_ID=$1
  local REGION=$2

  PUBLIC_AMI_INFO=

  echo_progress Make AMI $AMI_ID from $REGION region public
  echo_progress Wait for AMI to become available
  wait_for_ami_ready $AMI_ID "--region $REGION"

  while test x"$PUBLIC_AMI_INFO" = x""
  do
    make_ami_private $AMI_ID "--region $REGION"
    get_own_ami_info $AMI_ID "--region $REGION"
    make_ami_public $AMI_ID "--region $REGION"
    get_own_ami_info $AMI_ID "--region $REGION"

    for i in 1 2 3 4 5
    do
      sleep 5
      PUBLIC_AMI_INFO=`get_public_ami_info $AMI_ID "--region $REGION"`
      echo $i/5: $PUBLIC_AMI_INFO

      if test x"$PUBLIC_AMI_INFO" != x""; then
        return 0
      fi
    done

  done
}

make_replicas_public() {
  while read AMI; do
    if test x"$AMI" != x""; then
      make_replica_public $AMI
    fi
  done
}

get_image_type() {
    qemu-img info $1 | grep "file format" | awk '{print $NF}'
}
