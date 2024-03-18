#!/bin/bash

run_test()
{
  local TEST_COMMAND="$1"
  local RUN_OPTIONS="$2"
  echo "###### Running $1"
  scripts/run.py --execute="--mount-fs=ext,/dev/vblk1,/data $TEST_COMMAND" --second-disk-image ./ext_images/ext4.img $RUN_OPTIONS
}

if [[ "$1" != "" ]]; then
   run_test "$1" "$2"
   exit 0
fi

BASE='/data'
#BASE='/data/native-example'
#BASE='/data/TEST1'

run_test "/find /data -ls"

run_test "/mkdir -p $BASE"

#Delete if exists, touch, stat and cat
run_test "/rm -f $BASE/JAJA"
run_test "/touch $BASE/JAJA"
run_test "/stat $BASE/JAJA"
run_test "/cat $BASE/JAJA"
run_test "/rm $BASE/JAJA"

#Delete if exists, copy to test write, stat and cat
run_test "/rm -f $BASE/JAJA"
run_test "/cp /proc/mounts $BASE/JAJA"
run_test "/stat $BASE/JAJA"
run_test "/cat $BASE/JAJA"
run_test "/rm $BASE/JAJA"

#Make empty dir, stat and list
run_test "/rm -rf $BASE/DIR"
run_test "/mkdir $BASE/DIR"
run_test "/stat $BASE/DIR"
run_test "/rmdir $BASE/DIR"

#Make empty dir, stat and list
run_test "/rm -rf $BASE/DIR"
run_test "/mkdir $BASE/DIR"
run_test "/stat $BASE/DIR"
run_test "/ls -la $BASE/DIR"
run_test "/mkdir $BASE/DIR/SUBDIR"
run_test "/cp /proc/mounts $BASE/DIR/file1"
run_test "/touch $BASE/DIR/file2"
run_test "/cp $BASE/DIR/file1 $BASE/DIR/file3"
run_test "/find $BASE/DIR -ls"
run_test "/ls -la $BASE/DIR"
run_test "/stat -f $BASE/DIR"
run_test "/rm -rf $BASE/DIR"

#run_test "$BASE/hello-static"

#Test symlinks and hardlinks
run_test "/rm -rf $BASE/DIR"
run_test "/mkdir $BASE/DIR"
run_test "/ln -s /proc/mounts $BASE/DIR/symlink1"
run_test "/cat $BASE/DIR/symlink1"
run_test "/readlink -f $BASE/DIR/symlink1"
run_test "/touch $BASE/DIR/file1"
run_test "/ln $BASE/DIR/file1 $BASE/DIR/file1_hard"
run_test "/cp /proc/mounts $BASE/DIR/file1_hard"
run_test "/cat $BASE/DIR/file1"
run_test "/cat $BASE/DIR/file1_hard"
run_test "/ls -la $BASE/DIR"
run_test "/rm -rf $BASE/DIR"

#Test rename
run_test "/rm -rf $BASE/DIR2"
run_test "/mkdir $BASE/DIR2"
run_test "/cp /proc/mounts $BASE/DIR2/file1"
run_test "/mv $BASE/DIR2/file1 $BASE/DIR2/file2"
run_test "/mkdir $BASE/DIR2/SUBDIR1"
run_test "/mkdir $BASE/DIR2/SUBDIR2"
run_test "/cp /proc/mounts $BASE/DIR2/SUBDIR2"
run_test "/mv $BASE/DIR2/SUBDIR2 $BASE/DIR2/SUBDIR1"
run_test "/mv $BASE/DIR2/SUBDIR1 $BASE/DIR2/SUBDIR3"
run_test "/mv $BASE/DIR2/file2 $BASE/DIR2/SUBDIR3"
run_test "/find $BASE/DIR2 -ls"

#Test truncate
run_test "/rm -rf $BASE/DIR3"
run_test "/mkdir $BASE/DIR3"
run_test "/cp /proc/mounts $BASE/DIR3/file1"
run_test "/stat $BASE/DIR3/file1"
run_test "/truncate -s 64 $BASE/DIR3/file1"
run_test "/stat $BASE/DIR3/file1"
run_test "/rm -rf $BASE/DIR3"

#Test "cp -rf" and "rm -rf"
run_test "/rm -rf $BASE/DIR4"
run_test "/mkdir $BASE/DIR4"
run_test "cp -rf /data/fs $BASE/DIR4"
run_test "find $BASE/DIR4 -ls"
run_test "stat $BASE/DIR4"
#run_test "rm -rf $BASE/DIR4/*" - does not work?
run_test "rm -rf $BASE/DIR4"
