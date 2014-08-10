#!/bin/sh
#
#  Copyright (C) 2013 Cloudius Systems, Ltd.
#
#  This work is open source software, licensed under the terms of the
#  BSD license as described in the LICENSE file in the top-level directory.
#

. `dirname $0`/ec2-utils.sh

PARAM_HELP="--help"
PARAM_PRIVATE_ONLY="--private-ami-only"
PARAM_INSTANCE="--instance-only"
PARAM_IMAGE="--override-image"
PARAM_VERSION="--override-version"
PARAM_REGIONS="--override-regions"
PARAM_SMALL="--small-instances"
PARAM_MODULES="--modules-list"
PARAM_REGION="--region"
PARAM_ZONE="--zone"
PARAM_PLACEMENT_GROUP="--placement-group"
PARAM_IMAGE_SIZE="--image-size"

MODULES_LIST=default

print_help() {
 cat <<HLPEND

This script is intended to release OSv versions to Amazon EC2 service.
To start release process run "git checkout [options] <commit to be released> && $0"
from top of OSv source tree.

This script requires following Amazon credentials to be provided via environment variables:

    export AWS_ACCESS_KEY_ID=<Access key ID>
    export AWS_SECRET_ACCESS_KEY<Secret access key>

    See http://docs.aws.amazon.com/AWSSimpleQueueService/latest/SQSGettingStartedGuide/AWSCredentials.html
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
    $PARAM_SMALL - create AMI suitable for small and micro instances
    $PARAM_MODULES <modules list> - list of modules to build (incompatible with $PARAM_IMAGE and $PARAM_INSTANCE)
    $PARAM_REGION <region> - AWS region to work in
    $PARAM_ZONE <availability zone> - AWS availability zone to work in
    $PARAM_PLACEMENT_GROUP <placement group> - Placement group for instances created by this script
    $PARAM_IMAGE_SIZE <image size> - size of image to build (unit is MB)

HLPEND
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
    "$PARAM_SMALL")
      SMALL_INSTANCE=1
      shift
      ;;
    "$PARAM_MODULES")
      MODULES_LIST=$2
      shift 2
      ;;
    "$PARAM_REGION")
      AWS_DEFAULT_REGION=$2
      shift 2
      ;;
    "$PARAM_ZONE")
      OSV_AVAILABILITY_ZONE=$2
      shift 2
      ;;
    "$PARAM_PLACEMENT_GROUP")
      OSV_PLACEMENT_GROUP=$2
      shift 2
      ;;
    "$PARAM_IMAGE_SIZE")
      if [ $2 -ne "" ]
      then
        IMAGE_SIZE="fs_size_mb=$2"
      fi
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

import_osv_volume() {
 ec2-import-volume $OSV_VOLUME \
                                 -f raw \
                                 -b $OSV_BUCKET \
                                 -z `get_availability_zone` \
                                 $EC2_CREDENTIALS | tee /dev/tty | ec2_response_value IMPORTVOLUME TaskId
}

get_template_ami_id() {
 if test x"$SMALL_INSTANCE" = x""; then
  aws ec2 describe-images --filters='Name=name,Values=OSv-v*IPerf' | get_json_value '["Images"][0]["ImageId"]'
 else
  aws ec2 describe-images --filters='Name=name,Values=OSv-v*small' | get_json_value '["Images"][0]["ImageId"]'
 fi
}

get_template_instance_type() {
 if test x"$SMALL_INSTANCE" = x""; then
  echo c3.large
 else
  echo t1.micro
 fi
}

launch_template_instance() {
 if test x"$OSV_PLACEMENT_GROUP" != x""; then
  PLACEMENT="--placement-group $OSV_PLACEMENT_GROUP"
 else
  PLACEMENT=""
 fi

 local TEMPLATE_AMI_ID=$1
 local TEMPLATE_INSTANCE_TYPE=$2

 ec2-run-instances $TEMPLATE_AMI_ID --availability-zone `get_availability_zone` \
                                                  --instance-type $TEMPLATE_INSTANCE_TYPE \
                                                  $PLACEMENT \
                                                  | tee /dev/tty | ec2_response_value INSTANCE INSTANCE
}

get_availability_zone() {

 if test x"$OSV_AVAILABILITY_ZONE" = x""; then
     OSV_AVAILABILITY_ZONE=`ec2-describe-availability-zones | ec2_response_value AVAILABILITYZONE AVAILABILITYZONE | head -1`
 fi

 echo $OSV_AVAILABILITY_ZONE
}

list_additional_regions() {

 list_regions | grep -v $AWS_DEFAULT_REGION

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

BUCKET_CREATED=0

while true; do

    echo_progress Releasing version $OSV_VER \(modules $MODULES_LIST\)
    amend_rstatus Release status for version $OSV_VER \(modules $MODULES_LIST\)

    if test x"$AWS_ACCESS_KEY_ID" = x"" || test x"$AWS_SECRET_ACCESS_KEY" = x""; then
        handle_error No AWS credentials found. Please define AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY.
        break;
    fi

    if test x"$DONT_BUILD" != x"1"; then
        echo_progress Building from the scratch
        make clean && git submodule update && make external && make -j `nproc` $IMAGE_SIZE img_format=raw image=$MODULES_LIST

        if test x"$?" != x"0"; then
            handle_error Build failed.
            break;
        fi
    else
       IMAGE_FORMAT=`get_image_type $OSV_VOLUME`
       if test x"$IMAGE_FORMAT" != x"raw"; then
            handle_error Image format \"$IMAGE_FORMAT\" is not supported by EC2, please build a \"raw\" image.
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

    TEMPLATE_AMI_ID=`get_template_ami_id`
    TEMPLATE_INSTANCE_TYPE=`get_template_instance_type`

    if test x"$?" != x"0"; then
        handle_error Failed to rename the new volume.
        break;
    fi

    echo_progress Creating new template instance from AMI $TEMPLATE_AMI_ID
    echo_progress Region: $AWS_DEFAULT_REGION, Zone: $OSV_AVAILABILITY_ZONE, Group: $OSV_PLACEMENT_GROUP
    INSTANCE_ID=`launch_template_instance $TEMPLATE_AMI_ID $TEMPLATE_INSTANCE_TYPE`

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
    AMI_ID=`create_ami_by_instance $INSTANCE_ID $OSV_VER "Cloudius OSv demo $VERSION"`
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
