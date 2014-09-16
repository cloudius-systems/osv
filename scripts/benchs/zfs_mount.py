#!/usr/bin/env python

import subprocess
import sys
import re
import os
import argparse
from math import sqrt

devnull = open('/dev/null', 'w')

def run_zfsmount_bench(total_samples):
    samples = 0
    square_deviations = 0.0
    stats_min = float("inf")
    stats_max = 0.0
    stats_avg = 0.0
    stats_stdev = 0.0
    stats_total = 0.0

    print "id\ttime\tmin\tmax\tavg\tstdev"
    print "--\t----\t---\t---\t---\t-----"
    for sample in range(1, total_samples + 1):
        osv = subprocess.Popen('./scripts/run.py -e "tests/misc-fs-stress.so mumble"', shell = True, stdout=devnull)
        osv.wait()

        osv = subprocess.Popen('./scripts/run.py -e "--bootchart mumble"', shell = True, stdout=subprocess.PIPE)
        line = ""
        while True:
            line = osv.stdout.readline()
            if line == '':
                print "error: --bootchart isn\'t reporting the ZFS mount time!"
                sys.exit(1)
            elif 'ZFS mounted' in line:
                break
            elif 'spa_load_impl' in line: # debugging purposes
                print line
        mount_time = float(re.findall("\d+.\d+", line)[1])

        if mount_time < stats_min:
            stats_min = mount_time
        if mount_time > stats_max:
            stats_max = mount_time
        stats_total += mount_time
        samples += 1
        stats_avg = stats_total / float(samples)
        square_deviations += (mount_time - stats_avg) ** 2
        stats_stdev = sqrt(square_deviations / samples)

        print "%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f" % (sample, mount_time, stats_min, stats_max, stats_avg, stats_stdev)
        osv.wait()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog='zfs_mount')
    parser.add_argument("-s", "--samples", action="store", default="5",
                        help="specify number of samples")
    if not os.path.exists("./arch/x64"):
        print "Please, run this script from the OSv root directory"
    if not os.path.exists("./build/release/tests/misc-fs-stress.so"):
        print "error: misc-fs-stress.so wasn\'t found compiled in the build directory!\n"\
              "Please, compile OSv as follows: 'make image=tests'"
        sys.exit(1)
    cmdargs = parser.parse_args()
    print "Running ZFS mount time benchmark, total samples: %s" % cmdargs.samples
    print "unit of time: millisecond (ms)"
    run_zfsmount_bench(int(cmdargs.samples))
