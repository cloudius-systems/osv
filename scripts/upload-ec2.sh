#!/bin/sh -e
#
#  Copyright (C) 2013 Cloudius Systems, Ltd.
#
#  This work is open source software, licensed under the terms of the
#  BSD license as described in the LICENSE file in the top-level directory.
#

PARAM_HELP="--help"

print_help() {
 cat <<HLPEND

This script is intended to upload prebuilt OSv images to Amazon EC2 service.
The script expects a list of images on standard input and supported instance size
directives on input.

Input format:

<version/name>
<QCOW image URL>
<instance types to support (small|large)>
<version/name>
<QCOW image URL>
<instance types to support (small|large)>
...

The script uses release-ec2.sh internally, see its help screen for configuration
and AWS credentials issues.

HLPEND
}

upload_regions="us-east-1"

echo_progress() {
 echo $(tput setaf 3)">>> $*"$(tput sgr0)
}

upload_image() {

 local working_dir=upload-ec2-working-dir

 local IMAGE_NAME=$1
 local IMAGE_URL=$2
 local SUPPORTED_INSTANCE_TYPES=$3
 echo_progress Uploading image $IMAGE_NAME from $IMAGE_URL

 rm -rf $working_dir
 mkdir $working_dir
 local last_cwd=`pwd`
 cd $working_dir

 echo_progress Downloading the image

 wget $IMAGE_URL

 echo_progress Converting the image to RAW

 local file_name=`ls`
 qemu-img convert -f qcow2 -O raw $file_name ${file_name}.raw

 echo_progress Reading supported instance sizes

 if test x"$SUPPORTED_INSTANCE_TYPES" = x"small"; then
   local EXTRA_PARAMETERS="--small-instances"
   echo AMI will support all instance types including small and micro
 else
   echo AMI will support large instance types only
 fi

 echo_progress Running the release script

 cd $last_cwd
 `dirname $0`/release-ec2.sh --override-version $name \
                             --override-regions \"$upload_regions\" \
                             --override-image $working_dir/${file_name}.raw \
                             $EXTRA_PARAMETERS

 echo_progress Cleaning up

 rm -rf $working_dir

}

case "$1" in
 "$PARAM_HELP")
   print_help
   exit 0
   ;;
esac

while read name; do
    read url
    read instance_size
    upload_image $name $url $instance_size
done

echo_progress Done!
