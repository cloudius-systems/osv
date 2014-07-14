#!/usr/bin/env python
import metadata
import argparse
import tempfile
import shutil
import os

def write_file(path, contents):
    with open(path, "w") as f:
        f.write(contents)

def read_file(path):
    with open(path) as f:
        return f.read()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ami-id", default="ami-000000")
    parser.add_argument("--ami-launch-index", default="1")
    parser.add_argument("--reservation-id", default="r-00000000")
    parser.add_argument("--local-ipv4", default="127.0.0.1")
    parser.add_argument("--public-hostname", default="localhost")
    parser.add_argument("-u", "--user-data")
    parser.add_argument("-f", "--user-data-from-file")
    args = parser.parse_args()

    dir = tempfile.mkdtemp()
    try:
        os.makedirs(dir + '/latest/meta-data')

        write_file(dir + "/latest/meta-data/ami-id", args.ami_id)

        write_file(dir + "/latest/meta-data/ami-launch-index", args.ami_launch_index)

        write_file(dir + "/latest/meta-data/reservation-id", args.reservation_id)

        write_file(dir + "/latest/meta-data/local-ipv4", args.local_ipv4)

        write_file(dir + "/latest/meta-data/public-hostname", args.public_hostname)

        if args.user_data_from_file:
            if args.user_data:
                raise Exception('-u|--user-data cannot be set together with -f|--user-data-from-file')
            write_file(dir + "/latest/user-data", read_file(args.user_data_from_file))
        elif args.user_data:
            write_file(dir + "/latest/user-data", args.user_data)
        else:
            write_file(dir + "/latest/user-data", "")

        metadata.start_server(dir)
    finally:
        shutil.rmtree(dir)
