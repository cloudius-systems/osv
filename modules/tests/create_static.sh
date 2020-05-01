#!/bin/bash

THIS_PATH=$(readlink -f $0)
THIS_DIRECTORY=$(dirname ${THIS_PATH})
OUT=$1
MANIFEST=$2
FS=${3-zfs}

#Clean the directory
rm -rf ${OUT}/tests/static

if [ ${FS} = "rofs" ]
then
	mkdir -p ${OUT}/tests/static/rofs/tst-readdir-empty
	mkdir -p ${OUT}/tests/static/rofs/tst-readdir
	echo "Content" > ${OUT}/tests/static/rofs/tst-readdir/aaa

	mkdir -p ${OUT}/tests/static/rofs/tst-chdir
	echo "Content" > ${OUT}/tests/static/rofs/tst-chdir/f

	mkdir -p ${OUT}/tests/static/rofs/tst-symlink
	echo "Content" > ${OUT}/tests/static/rofs/tst-symlink/f1
	cd ${OUT}/tests/static/rofs/tst-symlink && ln -s f1 f2_AAA

	echo '/rofs/tst-readdir-empty: ./tests/static/rofs/tst-readdir-empty' >> ${THIS_DIRECTORY}/${MANIFEST}
	echo '/rofs/tst-chdir/f: ./tests/static/rofs/tst-chdir/f' >> ${THIS_DIRECTORY}/${MANIFEST}
	echo '/rofs/tst-readdir/aaa: ./tests/static/rofs/tst-readdir/aaa' >> ${THIS_DIRECTORY}/${MANIFEST}
	echo '/rofs/tst-symlink/f1: ./tests/static/rofs/tst-symlink/f1' >> ${THIS_DIRECTORY}/${MANIFEST}
	echo '/rofs/tst-symlink/f2_AAA: ->/rofs/tst-symlink/f1' >> ${THIS_DIRECTORY}/${MANIFEST}

	#This file is around 700K so it is a good test sample for the concurrent test
	echo '/rofs/mmap-file-test1: ./core/spinlock.o' >> ${THIS_DIRECTORY}/${MANIFEST}
	echo '/rofs/mmap-file-test2: ./core/spinlock.o' >> ${THIS_DIRECTORY}/${MANIFEST}
else
	echo '/tmp/mmap-file-test1: ./core/spinlock.o' >> ${THIS_DIRECTORY}/${MANIFEST}
	echo '/tmp/mmap-file-test2: ./core/spinlock.o' >> ${THIS_DIRECTORY}/${MANIFEST}
fi
