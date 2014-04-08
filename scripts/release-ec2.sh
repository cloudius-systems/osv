#!/bin/sh
#
#  Copyright (C) 2013 Cloudius Systems, Ltd.
#
#  This work is open source software, licensed under the terms of the
#  BSD license as described in the LICENSE file in the top-level directory.
#

PARAM_HELP="--help"
PARAM_PRIVATE_ONLY="--private-ami-only"
PARAM_INSTANCE="--instance-only"
PARAM_IMAGE="--override-image"
PARAM_VERSION="--override-version"
PARAM_REGIONS="--override-regions"

print_help() {
 cat <<HLPEND

This script is intended to release OSv versions to Amazon EC2 service.
To start release process run "git checkout [options] <commit to be released> && $0"
from top of OSv source tree.

This script requires following Amazon credentials to be provided via environment variables:

    AWS_ACCESS_KEY_ID=<Access key ID>
    AWS_SECRET_ACCESS_KEY<Secret access key>

    See http://docs.aws.amazon.com/AWSSimpleQueueService/latest/SQSGettingStartedGuide/AWSCredentials.html
    for more details

    EC2_PRIVATE_KEY=<Location of EC2 private key file>
    EC2_CERT=<Location of EC2 certificate file>

    See http://docs.aws.amazon.com/AWSSecurityCredentials/1.0/AboutAWSCredentials.html#X509Credentials
    for more details

This script assumes following packages are installed and functional:

    1. Amazon EC2 API Tools (http://aws.amazon.com/developertools/351)

       Installation and configuration instructions available at:
       http://docs.aws.amazon.com/AWSEC2/latest/UserGuide/SettingUp_CommandLine.html

    2. AWS Command Line Interface (http://aws.amazon.com/cli/)

       On Linux may be installed via pip:
       pip install awscli

This script receives following command line arguments:

    $PARAM_HELP - print this help screen and exit
    $PARAM_IMAGE <image file> - do not rebuild OSv, upload specified image instead
    $PARAM_VERSION <version string> - do not generate version based on repository, use specified string instead
    $PARAM_PRIVATE_ONLY - do not publish or replicate AMI - useful for pre-release build verification
    $PARAM_INSTANCE - do not rebuild, upload existing image and stop afer instance creation - useful for development phase
    $PARAM_REGIONS <regions list> - replicate to specified regions only

HLPEND
}

timestamp() {
 echo `date -u +'%Y-%m-%dT%H-%M-%SZ'`
}

while test "$#" -ne 0
do
  case "$1" in
    "$PARAM_IMAGE")
      OSV_VOLUME=$2
      DONT_BUILD=1
      shift 2
      ;;
    "$PARAM_VERSION")
      OSV_VER=$2
      shift 2
      ;;
    "$PARAM_PRIVATE_ONLY")
      DONT_PUBLISH=1
      shift
      ;;
    "$PARAM_INSTANCE")
      INSTANCE_ONLY=1
      DONT_BUILD=1
      shift
      ;;
    "$PARAM_REGIONS")
      REGIONS_LIST=$2
      shift 2
      ;;
    "$PARAM_HELP")
      print_help
      exit 0
      ;;
    *)
      shift
      ;;
    esac
done

if test x"$OSV_VER" = x""; then
    OSV_VER=$(`dirname $0`/osv-version.sh)
fi

if test x"$OSV_VOLUME" = x""; then
    OSV_VOLUME=build/release/usr.img
fi

OSV_RSTATUS=rstatus-$OSV_VER-`timestamp`.txt
OSV_BUCKET=osv-$OSV_VER-$USER-at-`hostname`-`timestamp`

EC2_CREDENTIALS="-o $AWS_ACCESS_KEY_ID -w $AWS_SECRET_ACCESS_KEY"
S3_CREDENTIALS="-O $AWS_ACCESS_KEY_ID -W $AWS_SECRET_ACCESS_KEY"

export AWS_DEFAULT_REGION=us-east-1
OSV_INITIAL_ZONE="${AWS_DEFAULT_REGION}a"

#We use OSv-v0.03 AMI in us-east-1 as a template
TEMPLATE_AMI_ID=ami-45d2882c

amend_rstatus() {
 echo \[`timestamp`\] $* >> $OSV_RSTATUS
}

handle_error() {
 echo $(tput setaf 1)">>> [`timestamp`] ERROR: $*"$(tput sgr0)
 echo
 amend_rstatus ERROR: $*
 amend_rstatus Release FAILED.
}

echo_progress() {
 echo $(tput setaf 3)">>> $*"$(tput sgr0)
}

get_json_value() {
 python -c "import json,sys;obj=json.load(sys.stdin);print obj$*"
}

ec2_response_value() {
 local ROWID=$1
 local KEY=$2
 grep $ROWID | sed -n "s/^.*$KEY\s\+\(\S\+\).*$/\1/p"
}

import_osv_volume() {
 $EC2_HOME/bin/ec2-import-volume $OSV_VOLUME \
                                 -f raw \
                                 -b $OSV_BUCKET \
                                 -z $OSV_INITIAL_ZONE \
                                 $EC2_CREDENTIALS \
                                 $S3_CREDENTIALS | tee /dev/tty | ec2_response_value IMPORTVOLUME TaskId
}

get_volume_conversion_status() {
 local TASKID=$1
 $EC2_HOME/bin/ec2-describe-conversion-tasks $TASKID | tee /dev/tty | ec2_response_value IMPORTVOLUME Status
}

get_created_volume_id() {
 local TASKID=$1
 $EC2_HOME/bin/ec2-describe-conversion-tasks $TASKID | tee /dev/tty | ec2_response_value DISKIMAGE VolumeId
}

rename_object() {
 local OBJID=$1
 local OBJNAME=$2
 $EC2_HOME/bin/ec2-create-tags $OBJID --tag Name="$OBJNAME"
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

launch_template_instance() {
 $EC2_HOME/bin/ec2-run-instances $TEMPLATE_AMI_ID --availability-zone $OSV_INITIAL_ZONE | tee /dev/tty | ec2_response_value INSTANCE INSTANCE
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

 local STATE="unknown"

 while test x"$STATE" != x"$REQUESTED_STATE"
 do
   sleep 5
   STATE=`get_instance_state $INSTANCE_ID`
   echo Instance is $STATE
 done
}

wait_for_instance_startup() {
 wait_for_instance_state $1 running
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

stop_instance_forcibly() {
 $EC2_HOME/bin/ec2-stop-instances $1 --force
}

create_ami_by_instance() {
 local INSTANCE_ID=$1
 local VERSION=$2
 aws ec2 create-image --instance-id $INSTANCE_ID --name OSv-$VERSION --description "Cloudius OSv demo $VERSION" | get_json_value '["ImageId"]'
}

copy_ami_to_region() {
 local AMI_ID=$1
 local REGION=$2

 aws ec2 --region $REGION copy-image --source-region $AWS_DEFAULT_REGION --source-image-id $AMI_ID | get_json_value '["ImageId"]'
}

make_ami_public() {
 local AMI_ID=$1
 shift

 $EC2_HOME/bin/ec2-modify-image-attribute $AMI_ID --launch-permission --add all $*
}

make_ami_private() {
 local AMI_ID=$1
 shift

 $EC2_HOME/bin/ec2-modify-image-attribute $AMI_ID --launch-permission --remove all $*
}

list_regions() {

 if test x"$REGIONS_LIST" = x""; then
     $EC2_HOME/bin/ec2-describe-regions | ec2_response_value REGION REGION
 else
     for region in $REGIONS_LIST; do echo $region; done
 fi

}

list_additional_regions() {

 list_regions | grep -v $AWS_DEFAULT_REGION

}

get_own_ami_info() {
 local AMI_ID=$1
 shift

 $EC2_HOME/bin/ec2-describe-images $* | grep $AMI_ID
}

get_public_ami_info() {
 local AMI_ID=$1
 shift

 $EC2_HOME/bin/ec2-describe-images -x all $* | grep $AMI_ID
}

replicate_ami() {
 local AMI_ID=$1
 local REGIONS="`list_additional_regions`"

 for REGION in $REGIONS
 do
     echo Copying to $REGION
     local AMI_IN_REGION=`copy_ami_to_region $AMI_ID $REGION`

     if test x"$AMI_IN_REGION" = x; then
         handle_error Failed to replicate AMI to $REGION region.
         return 1;
     fi

     amend_rstatus AMI ID in region $REGION is $AMI_IN_REGION
     NL='
'
     REPLICAS_LIST="$REPLICAS_LIST${NL}$AMI_IN_REGION $REGION"
 done

 return 0;
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

BUCKET_CREATED=0

while true; do

    echo_progress Releasing version $OSV_VER
    amend_rstatus Release status for version $OSV_VER

    if test x"$DONT_BUILD" != x"1"; then
        echo_progress Building from the scratch
        make clean && git submodule update && make external && make -j `nproc` img_format=raw

        if test x"$?" != x"0"; then
            handle_error Build failed.
            break;
        fi
    fi

    echo_progress Creating bucket $OSV_BUCKET
    aws s3api create-bucket --bucket $OSV_BUCKET

    if test x"$?" != x"0"; then
        handle_error Failed to create S3 bucket.
        break;
    fi

    BUCKET_CREATED=1

    echo_progress Importing volume $OSV_VOLUME
    IMPORT_TASK=`import_osv_volume`

    if test x"$IMPORT_TASK" = x; then
        handle_error Failed to import OSv volume.
        break;
    fi

    echo Import volume task ID is $IMPORT_TASK

    echo_progress Waiting for conversion tasks to complete
    wait_import_completion $IMPORT_TASK

    echo_progress Reading newly created volume ID
    VOLUME_ID=`get_created_volume_id $IMPORT_TASK`

    if test x"$VOLUME_ID" = x; then
        handle_error Failed to read created volume ID.
        break;
    fi

    echo Created volume ID is $VOLUME_ID

    echo_progress Renaming newly created volume OSv-$OSV_VER
    rename_object $VOLUME_ID OSv-$OSV_VER

    if test x"$?" != x"0"; then
        handle_error Failed to rename the new volume.
        break;
    fi

    echo_progress Creating new template instance from AMI $TEMPLATE_AMI_ID
    INSTANCE_ID=`launch_template_instance`

    if test x"$INSTANCE_ID" = x; then
        handle_error Failed to create template instance.
        break;
    fi

    echo New instance ID is $INSTANCE_ID

    echo_progress Wait for instance to become running
    wait_for_instance_startup $INSTANCE_ID

    echo_progress Renaming newly created instance OSv-$OSV_VER
    rename_object $INSTANCE_ID OSv-$OSV_VER

    if test x"$?" != x"0"; then
        handle_error Failed to rename template instance.
        break;
    fi

    echo_progress Stopping newly created instance
    stop_instance_forcibly $INSTANCE_ID

    if test x"$?" != x"0"; then
        handle_error Failed to stop template instance.
        break;
    fi

    echo_progress Waiting for instance to become stopped
    wait_for_instance_shutdown $INSTANCE_ID

    echo_progress Replacing template instance volume with uploaded OSv volume
    ORIGINAL_VOLUME_ID=`get_instance_volume_id $INSTANCE_ID`

    if test x"$ORIGINAL_VOLUME_ID" = x""; then
        handle_error Failed to read original volume ID.
        break;
    fi

    echo Original volume ID is $ORIGINAL_VOLUME_ID

    echo_progress Detaching template instance volume
    detach_volume $ORIGINAL_VOLUME_ID

    if test x"$?" != x"0"; then
        handle_error Failed to detach original volume from template instance.
        break;
    fi

    echo_progress Waiting for volume to become available
    wait_for_volume_detach $ORIGINAL_VOLUME_ID

    echo_progress Attaching uploaded volume to template instance
    attach_volume $VOLUME_ID $INSTANCE_ID

    if test x"$?" != x"0"; then
        handle_error Failed to attach OSv volume to template instance.
        break;
    fi

    echo_progress Waiting for uploaded volume to become attached
    wait_for_volume_attach $VOLUME_ID

    if test x"$INSTANCE_ONLY" = x"1"; then
        amend_rstatus Development instance $INSTANCE_ID created successfully.
        break;
    fi

    echo_progress Creating AMI from resulting instance
    AMI_ID=`create_ami_by_instance $INSTANCE_ID $OSV_VER`
    echo AMI ID is $AMI_ID

    if test x"$AMI_ID" = x""; then
        handle_error Failed to create AMI from template instance.
        break;
    fi

    amend_rstatus AMI ID in region $AWS_DEFAULT_REGION is $AMI_ID

    echo Waiting for AMI to become availabe
    wait_for_ami_ready $AMI_ID

    if test x"$DONT_PUBLISH" = x"1"; then
        amend_rstatus Private AMI created successfully.
        break;
    fi

    echo_progress Making AMI public
    make_ami_public $AMI_ID

    if test x"$?" != x"0"; then
        handle_error Failed to publish OSv AMI.
        break;
    fi

    echo_progress Replicating AMI to existing regions
    REPLICAS_LIST=
    replicate_ami $AMI_ID

    if test x"$?" != x"0"; then
        handle_error Failed to initiate AMI replication. Replicate $AMI_ID in $AWS_DEFAULT_REGION region manually.
        break;
    fi

    echo_progress Making replicated images public \(work-around AWS bug\)
    make_replicas_public <<END_REPLICAS
$REPLICAS_LIST
END_REPLICAS

    amend_rstatus Release SUCCEEDED

break

done

echo_progress Cleaning-up intermediate objects

if test x"$INSTANCE_ONLY" != x"1"; then

    if test x"$VOLUME_ID" != x""; then
        echo_progress Detaching imported OSv volume
        detach_volume $VOLUME_ID

        echo_progress Wait for imported OSv volume to become detached
        wait_for_volume_detach $VOLUME_ID

        echo_progress Deleting imported OSv volume
        delete_volume $VOLUME_ID
    fi

    if test x"$INSTANCE_ID" != x""; then
        echo_progress Deleting template instance
        delete_instance $INSTANCE_ID
    fi

fi

if test x"$ORIGINAL_VOLUME_ID" != x""; then
    echo_progress Deleting template instance volume
    delete_volume $ORIGINAL_VOLUME_ID
fi

if test x"$BUCKET_CREATED" != x"0"; then
    echo_progress Deleting bucket $OSV_BUCKET
    aws s3 rb s3://$OSV_BUCKET --force > /dev/null
fi

echo_progress Done. Release status is stored in $OSV_RSTATUS:
cat $OSV_RSTATUS
