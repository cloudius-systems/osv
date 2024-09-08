#!/bin/bash
#
#This script deploys latest built OSv image to Google cloud as a GCE vm
#
#It does so it two steps:
#1. Uploads the latest raw image under build/last/usr.img to Google cloud storage
#   and creates a compute image
#2. Creates new GCE instance of type specified by the optional INSTANCE_TYPE

#BEFORE running this please set your project it and zone
# gcloud config set project <YOUR_PROJECT_ID>
# gcloud config set compute/zone <YOUR_ZONE>, for example us-east1-c

ACTION=$1
GCS_FOLDER=$2
INSTANCE_NAME=$3
INSTANCE_TYPE=${4:-f1-micro}

if [[ "$ACTION" == "" || "$GCS_FOLDER" == "" || "$INSTANCE_NAME" == "" ]]; then
  echo "Usage: ./scripts/deploy_to_gce.sh create|delete|describe <GCS_FOLDER> <INSTANCE_NAME> <INSTANCE_TYPE> (optional)"
  exit -1
fi

if [[ "$ACTION" != "create" && "$ACTION" != "delete" && "$ACTION" != "describe" ]]; then
  echo "The 1st argument must be either 'create' or 'delete' or 'describe'" 
  exit -1
fi

if [[ ! "$INSTANCE_NAME" =~ ^[a-z][-a-z0-9]*$ ]]; then
  echo "Invalid INSTANCE_NAME! Should match regex [a-z][-a-z0-9]*"
  exit -1
fi

if [[ "$ACTION" == "create" ]]; then
pushd ./build/last > /dev/null
rm -f disk.raw
qemu-img convert -O raw usr.img disk.raw
tar -Szcf $INSTANCE_NAME.tar.gz disk.raw && rm disk.raw
popd > /dev/null
echo "Converted latest image to raw format and created image tarball; $(readlink -f $INSTANCE_NAME.tar.gz)"

#Copy to Google Storage
gsutil cp ./build/last/$INSTANCE_NAME.tar.gz gs://$GCS_FOLDER/$INSTANCE_NAME.tar.gz
echo "Copied $INSTANCE_NAME.tar.gz to gs://$GCS_FOLDER"

#Create GCE compute image
gcloud compute images create $INSTANCE_NAME --source-uri=gs://$GCS_FOLDER/$INSTANCE_NAME.tar.gz
echo "Created an GCE image for $INSTANCE_NAME"

#Create instance
echo "Creating new GCE instance $INSTANCE_NAME ..."
gcloud compute instances create $INSTANCE_NAME --image=$INSTANCE_NAME --machine-type=$INSTANCE_TYPE

echo "Created new GCE instance $INSTANCE_NAME!"
gcloud compute instances get-serial-port-output $INSTANCE_NAME

gcloud compute firewall-rules create "$INSTANCE_NAME-allow-8000" --allow tcp:8000 --source-tags=$INSTANCE_NAME --source-ranges=0.0.0.0/0 --description="Allows 8000"
gcloud compute firewall-rules create "$INSTANCE_NAME-allow-9000" --allow tcp:9000 --source-tags=$INSTANCE_NAME --source-ranges=0.0.0.0/0 --description="Allows 9000"
echo "Created firewall rules to allow traffic on ports 8000 and 9000"
elif [[ "$ACTION" == "delete" ]]; then
gcloud compute firewall-rules delete "$INSTANCE_NAME-allow-8000" --quiet
gcloud compute firewall-rules delete "$INSTANCE_NAME-allow-9000" --quiet
echo "Deleted firewall rules to allow traffic on ports 8000 and 9000"

gcloud compute instances delete $INSTANCE_NAME --quiet
echo "Deleted new GCE instance $INSTANCE_NAME!"

gcloud compute images delete $INSTANCE_NAME --quiet
echo "Deleted the GCE image for $INSTANCE_NAME"

gsutil rm gs://$GCS_FOLDER/$INSTANCE_NAME.tar.gz
echo "Deleted gs://$GCS_FOLDER/$INSTANCE_NAME.tar.gz"
else
gcloud compute instances describe $INSTANCE_NAME
fi

#curl http://34.148.84.220:8000/os/dmesg
#curl http://34.148.84.220:9000
