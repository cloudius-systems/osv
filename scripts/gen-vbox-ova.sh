#!/bin/sh

# Setup name and dir
name=osv
vmdir=~/VirtualBox\ VMs/$name
vdi_img=build/release/osv.vdi
ova_img=build/release/osv.ova

# Stop vm
VBoxManage controlvm $name poweroff  >/dev/null 2>&1
# Unreigster vm
VBoxManage unregistervm $name --delete  >/dev/null 2>&1
# Delete old ova
rm -f $ova_img

# Create and register the vm
VBoxManage createvm  --name $name -ostype Linux26_64
VBoxManage registervm "$vmdir/$name.vbox"

# Setup mem
VBoxManage modifyvm osv --memory 1024

# Setup SATA controller
make osv.vdi
cp $vdi_img "$vmdir/$name.vdi"
VBoxManage storagectl  $name  --name SATA --add sata --controller IntelAHCI
VBoxManage storageattach  $name --storagectl SATA --port 0 --type hdd --medium "$vmdir/$name.vdi"

# Setup Network hostonly
VBoxManage modifyvm $name --nic1 hostonly
VBoxManage modifyvm $name --nictype1 virtio
VBoxManage modifyvm $name --hostonlyadapter1 vboxnet0

# Turn on HPET timer
VBoxManage modifyvm $name --hpet on

# Use ICH9 instead of piix
#VBoxManage modifyvm $name --chipset ich9

# Setup serial
VBoxManage modifyvm $name  --uart1 0x3f8 4
#VBoxManage modifyvm $name  --uartmode1 file "$vmdir/$name.log"

# Export ova
VBoxManage export osv -o build/release/osv.ova --ovf10 --vsys 0 --product "OSv" --vendor "Cloudius Systems"

echo "$PWD/$ova_img is created"
