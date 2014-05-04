#!/bin/sh

VMX_FILE=build/release/osv.vmx

cat << 'EOF' > $VMX_FILE
#!/usr/bin/vmware
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "8"
scsi0.present = "TRUE"
scsi0.virtualDev = "pvscsi"
memsize = "1024"
ethernet0.present = "TRUE"
ethernet0.connectionType = "nat"
ethernet0.virtualDev = "vmxnet3"
ethernet0.addressType = "generated"
pciBridge0.present = "TRUE"
pciBridge4.present = "TRUE"
pciBridge4.virtualDev = "pcieRootPort"
pciBridge4.functions = "8"
hpet0.present = "TRUE"
displayName = "osv"
guestOS = "ubuntu-64"
scsi0:0.present = "TRUE"
scsi0:0.fileName = "osv.vmdk"
floppy0.present = "FALSE"
serial0.present = "TRUE"
serial0.fileType = "network"
serial0.fileName = "telnet://127.0.0.1:10000"
EOF
