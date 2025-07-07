#!/bin/bash

argv0=${0##*/}
usage() {
	cat <<-EOF
	Manipulate ZFS images on host using OpenZFS - mount, unmount and build.

	Usage: ${argv0} mount <image_path> <partition> <pool_name> <filesystem> |
	                            build <image_path> <partition> <pool_name> <filesystem> <populate_image> |
	                            unmount <pool_name>

	Where:
	  image_path      path to a qcow2 or raw ZFS image; defaults to build/last/usr.img
	  partition       partition of disk above; defaults to 1
	  pool_name       name of ZFS pool; defaults to osv
	  filesystem      name of ZFS filesystem; defaults to zfs
	  populate_image  boolean value to indicate if the image should be populated with content
	                  from build/last/usr.manifest; defaults to true but only used with 'build' command

	Examples:
	  ${argv0} mount                                     # Mount OSv image from build/last/usr.img under /zfs
	  ${argv0} mount build/last/zfs_disk.img 1           # Mount OSv image from build/last/zfs_disk.img 2nd partition under /zfs
	  ${argv0} unmount                                   # Unmount OSv image from /zfs
	EOF
	exit ${1:-0}
}

source $(dirname $0)/disk-utils-common.sh

mount_image() {
	connect_image $1 $2

	local pool_name="$3"
	if [[ "" != $(zpool list -H ${pool_name} 2>/dev/null | grep ${pool_name}) ]]; then
		echo "There seems to be ${pool_name} already imported. Please export it first or run $0 unmount"
		return -1
	fi
	sudo zpool import -d ${device_path} ${pool_name}
	echo "Imported pool ${pool_name}"

	local filesystem="$4"
	local dataset="$pool_name/$filesystem"
	if [[ "" != $(df | grep "$dataset") ]]; then
		echo "There seems to be ${filesystem} already mounted. Please 'zfs unmount ${dataset}' it first or run $0 unmount"
		return -1
	fi
	sudo zfs mount ${dataset}
	echo "Mounted ${dataset} at /${filesystem}"
}

unmount_image() {
	local pool_name="$1"

        local zfs_unmounted=false
	if [[ "" != $(df | grep "$pool_name") ]]; then
		sudo zfs umount ${pool_name}
		zfs_unmounted=true
		echo "Unmounted ${pool_name} from /${filesystem}"
	fi

	local zpool_exported=false
	if [[ "" != $(zpool list -H ${pool_name} 2>/dev/null | grep ${pool_name}) ]]; then
		sudo zpool export ${pool_name}
		zpool_exported=true
		echo "Exported pool ${pool_name}"
	fi

        if [[ "$zpool_exported" != "true" || "$zfs_unmounted" != "true" ]]; then
		echo "Skipping to disconnect devices!"
		return -1
	fi

	disconnect_image
}

build_image() {
	connect_image $1 $2
        local vdev=${device_path:5}

	local pool_name="$3"
	if [[ "" != $(zpool list -H ${pool_name} 2>/dev/null | grep ${pool_name}) ]]; then
		echo "There seems to be ${pool_name} already imported. Please export it first or run $0 unmount"
		return -1
	fi

	local filesystem="$4"
	sudo zpool create -df -R / ${pool_name} ${vdev}
	sudo zpool set feature@lz4_compress=enabled ${pool_name}

	local dataset="$pool_name/$filesystem"
	sudo zfs create -u -o relatime=on ${dataset}
	sudo zfs set mountpoint=/ ${pool_name}
	sudo zfs set mountpoint=/${filesystem} ${pool_name}/${filesystem}
	sudo zfs set canmount=noauto ${pool_name}
	sudo zfs set canmount=noauto ${pool_name}/${filesystem}
	sudo zfs set compression=lz4 ${pool_name}
	sudo zfs mount ${dataset}

	local populate_image=$5
	if [[ "true" == "$populate_image" ]]; then
		pushd "$OSV_ROOT/build/release" && sudo "$OSV_ROOT/scripts/export_manifest.py" -m usr.manifest -e /zfs/ -D libgcc_s_dir="$libgcc_s_dir" && popd
	fi

	sudo zfs set compression=off ${pool_name}

	unmount_image $pool_name
}

sudo modprobe nbd
sudo modprobe zfs
if [[ $? != 0 ]]; then
	echo "OpenZFS does not seem to be installed!"
	exit 1
fi

OSV_ROOT="$(readlink -f $(dirname $0)/..)"

command=$1
shift
case $command in 
	mount)
		image_path=${1:-$OSV_ROOT/build/last/usr.img}
		partition=${2:-1}
		pool_name=${3:-osv}
		filesystem=${4:-zfs}
		mount_image $image_path $partition $pool_name $filesystem
		;;
	unmount)
		pool_name=${1:-osv}
		unmount_image $pool_name
		;;
	build)
		image_path=${1:-$OSV_ROOT/build/last/usr.img}
		partition=${2:-1}
		pool_name=${3:-osv}
		filesystem=${4:-zfs}
		populate_image=${5:-true}
		build_image $image_path $partition $pool_name $filesystem $populate_image
		;;
	*)
		usage
		;;
esac
#
# Once an image is mounted there are many useful commands one can use to inspect it
# -------------------
# Dataset related
# ------------------
# List all datasets
#   zfs list
#
# List datasets named 'osv/zfs'
#   zfs list osv/zfs
#
# List properties of the 'osv/zfs' filesystem
#   zfs get all osv/zfs
#
# -------------------
# Pool related
# -------------------
# List history of the 'osv' pool
#   sudo zpool history osv
#   sudo zpool history osv -l  # With extra details
#   sudo zpool history osv -li # With more details
#
# List events of the 'osv' pool
#   sudo zpool events osv
#   sudo zpool events osv -v   # With way more details
#
# List properties of the 'osv' pool
#   zpool get all osv
#
# List all pools
#   zpool list
#   zpool list -v # With vdevs
