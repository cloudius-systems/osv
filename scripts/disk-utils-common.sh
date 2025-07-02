#!/bin/bash

connect_image_to_nbd_device() {
	# Check if we have something connected to nbd0
	if [[ 0 != $(lsblk | grep nbd0 | wc -l) && "" == $(lsblk | grep nbd0 | grep " 0B") ]]; then
		echo "The device /dev/nbd0 is busy. Please disconnect it or run $0 unmount"
		return -1
	fi
	sudo -p "password for %p to activate qemu nbd kernel module:" modprobe nbd
	local image_path="$1"
	sudo qemu-nbd --connect /dev/nbd0 ${image_path} 1>/dev/null
	echo "Connected device /dev/nbd0 to the image ${image_path}"

	# Identify nbd device if it maps to a specific partition 
	if [[ "$2" != "" ]]; then
		local partition=$2
		local nbd_device=$(lsblk | grep part | grep -o "nbd0\S*" | head -n $partition | tail -1)
	else
		local nbd_device=$(lsblk | grep part | grep -o "nbd0\S*" | tail -1)
	fi
	if [[ "" == "$nbd_device" ]]; then
		nbd_device=nbd0
		echo "Assuming /dev/nbd0 as a device to mount to"
	fi
	device=$nbd_device
	device_path="/dev/$nbd_device"
}

connect_image_to_loop_device() {
	local image_path="$1"
	if [[ $(losetup | grep "$image_path" | wc -l) != 0 ]]; then
		echo "The $image_path is already created and mounted. Please unmount and delete it first!"
		exit 1
	fi
	local partition_offset=0
	if [[ "$2" != "" ]]; then
		local partition=$(($2+1))
		partition_offset=$($OSV_ROOT/scripts/imgedit.py getpartition_offset "-f raw $image_path" $partition)
	fi
	device_path=$(sudo -p "password for %p to run losetup to create loop device:" losetup -o $partition_offset -f --show ${image_path})
	device=${device_path:5}
	echo "Connected device $device to the image ${image_path}"
}

connect_image() {
	local image_path="$1"
	local image_format=$(qemu-img info ${image_path} | grep "file format")

	if [[ "$image_format" == *"qcow2"* ]]; then
 		connect_image_to_nbd_device $image_path $2
	elif [[ "$image_format" == *"raw"* ]]; then
 		connect_image_to_loop_device $image_path $2
	else
		echo "The file $image_path does not seem to be a valid qcow2 nor raw image!"
		return -1
	fi
}

disconnect_image() {
	local device=$1
	# Try NBD sevice
	if [[ "$device" != "" && "" != $(lsblk | grep nbd | grep $device) ]]; then
		sudo qemu-nbd --disconnect /dev/$device 1>/dev/null
		echo "Disconnected nbd device $device from the image"
		return 0
	elif [[ 0 != $(lsblk | grep nbd0 | wc -l) && "" == $(lsblk | grep nbd0 | grep " 0B") ]]; then
		sudo qemu-nbd --disconnect /dev/nbd0 1>/dev/null
		echo "Disconnected nbd device /dev/nbd0 from the image"
		return 0
	fi

	# Try loopback device
	if [[ "$device" != "" && "" != $(lsblk | grep loop | grep $device) ]]; then
		sudo losetup -d /dev/$device 1>/dev/null
		echo "Disconnected loop device $device from the image"
	elif [[ "" != $(lsblk | grep loop) ]]; then
		local device_path=$(losetup -ln | cut -d ' ' -f 1)
		sudo losetup -d $device_path 1>/dev/null
		echo "Disconnected loop device $device_path from the image"
	fi
}
