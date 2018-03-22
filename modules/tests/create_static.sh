#!/bin/bash

THIS_PATH=$(readlink -f $0)
THIS_DIRECTORY=$(dirname ${THIS_PATH})
OUT=$1
FS=${2-zfs}

#Clean the directory
rm -rf ${OUT}/tests/static

if [ ${FS} = "rofs" ]
then
	mkdir -p ${OUT}/tests/static/rofs/tst-readdir-empty
	mkdir -p ${OUT}/tests/static/rofs/tst-readdir
	touch ${OUT}/tests/static/rofs/tst-readdir/aaa

	mkdir -p ${OUT}/tests/static/rofs/tst-chdir
	touch ${OUT}/tests/static/rofs/tst-chdir/f

	mkdir -p ${OUT}/tests/static/rofs/tst-symlink
	touch ${OUT}/tests/static/rofs/tst-symlink/f1
	cd ${OUT}/tests/static/rofs/tst-symlink && ln -s f1 f2_AAA

	echo '/rofs/tst-readdir-empty: ./tests/static/rofs/tst-readdir-empty' >> ${THIS_DIRECTORY}/usr.manifest
	echo '/rofs/tst-chdir/f: ./tests/static/rofs/tst-chdir/f' >> ${THIS_DIRECTORY}/usr.manifest
	echo '/rofs/tst-readdir/aaa: ./tests/static/rofs/tst-readdir/aaa' >> ${THIS_DIRECTORY}/usr.manifest
	echo '/rofs/tst-symlink/f1: ./tests/static/rofs/tst-symlink/f1' >> ${THIS_DIRECTORY}/usr.manifest
	echo '/rofs/tst-symlink/f2_AAA: ->/rofs/tst-symlink/f1' >> ${THIS_DIRECTORY}/usr.manifest

	#This file is around 700K so it is a good test sample for the concurrent test
	echo '/rofs/mmap-file-test1: ./core/spinlock.o' >> ${THIS_DIRECTORY}/usr.manifest
	echo '/rofs/mmap-file-test2: ./core/spinlock.o' >> ${THIS_DIRECTORY}/usr.manifest
else
	echo '/tmp/mmap-file-test1: ./core/spinlock.o' >> ${THIS_DIRECTORY}/usr.manifest
	echo '/tmp/mmap-file-test2: ./core/spinlock.o' >> ${THIS_DIRECTORY}/usr.manifest
fi
