## Building Image with Ext4 Support

```bash    
./scripts/build fs=rofs image=libext,native-example -j$(nproc)
```

## Creating Disk with Ext4 Filesystem

### Create Empty Disk
```bash
mkdir -p ext_images
dd if=/dev/zero of=ext_images/ext4 bs=1M count=128
sudo mkfs.ext4 ext_images/ext4
```

### Mounting Disk as Loop Device
```bash
sudo losetup -o 0 -f --show ext_images/ext4
sudo mount /dev/loop0 ext_images/image

#Copy sample files from the host
cp -rf fs ext_images/image

#Unmount
sudo umount ext_images/image
sudo losetup -d /dev/loop0
    
qemu-img convert -f raw -O qcow2 ext_images/ext4 ext_images/ext4.img
```
    
## Running with Ext4 Disk

```bash
./scripts/run.py --execute='--mount-fs=ext,/dev/vblk1,/data /hello' --second-disk-image ./ext_images/ext4.img
```

or use the `test.sh`:

```bash
./modules/libext/test.sh '/find /data/ -ls'
```

## Checking the Disk

* Mount the disk as described above
* Run fsck

```bash
sudo fsck -n /dev/loop0
```

## Ext2/3/4 file system documentation
- https://blogs.oracle.com/linux/post/understanding-ext4-disk-layout-part-1
- https://blogs.oracle.com/linux/post/understanding-ext4-disk-layout-part-2
- https://adil.medium.com/ext4-filesystem-data-blocks-super-blocks-inode-structure-1afb95c8e4ab
- https://adil.medium.com/ext4-filesystem-extent-flex-bg-sparse-super-83f172d694c6
- https://adil.medium.com/ext4-file-system-delayed-allocation-dirty-data-blocks-35945a49fac5
