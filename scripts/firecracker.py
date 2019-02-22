#!/usr/bin/env python
#
# pip install requests-unixsocket
import sys
import os
import stat
import json
import subprocess
import time
from os.path import expanduser
from datetime import datetime
import requests_unixsocket


class ApiClient(object):
    def __init__(self, domain_socket_path):
        self.socket_path = domain_socket_path
        self.session = requests_unixsocket.Session()

    def api_socket_url(self, path):
        return "http+unix://%s%s" % (self.socket_path, path)

    def make_put_call(self, path, request_body):
        url = self.api_socket_url(path)
        res = self.session.put(url, data=json.dumps(request_body))
        print("%s: %s" % (path, res.status_code))
        if res.status_code != 204:
            print(res.text)
        return res.status_code

    def create_instance(self, kernel_image_path, cmdline):
        self.make_put_call('/boot-source', {
            'kernel_image_path': kernel_image_path,
            'boot_args': cmdline
        })

    def start_instance(self):
        self.make_put_call('/actions', {
            'action_type': 'InstanceStart'
        })

    def configure_logging(self):
        self.make_put_call('/logger', {
            "log_fifo": "log.fifo",
            "metrics_fifo": "metrics.fifo",
            "level": "Info",
            "show_level": True,
            "show_log_origin": True
        })


def print_time(msg):
    now = datetime.now()
    print("%s: %s" % (now.strftime('%H:%M:%S.%f'), msg))


# Check if firecracker is installed
home_dir = expanduser("~")
firecracker_path = os.path.join(home_dir, '.firecracker/firecracker')
if os.environ.get('FIRECRACKER_PATH'):
    firecracker_path = os.environ.get('FIRECRACKER_PATH')

# And offer to install if not found
if not os.path.exists(firecracker_path):
    download_url = 'https://github.com/firecracker-microvm/firecracker/releases/download/v0.14.0/firecracker-v0.14.0'
    answer = raw_input("Firecracker executable has not been found under %s. "
                       "Would you like to download it from %s and place it under %s? [y|Y]" %
                       (firecracker_path, download_url, firecracker_path))
    if answer.capitalize() != 'Y':
        print("Firecracker not available. Exiting ...")
        sys.exit(-1)

    directory = os.path.dirname(firecracker_path)
    if not os.path.exists(directory):
        os.mkdir(directory)
    subprocess.call(['wget', download_url, '-O', firecracker_path])
    os.chmod(firecracker_path, stat.S_IRUSR | stat.S_IXUSR)

# Firecracker is installed so lets start
print_time("Start")
socket_path = '/tmp/firecracker.socket'

# Delete socker file if exists
if os.path.exists(socket_path):
    os.unlink(socket_path)

# Start firecracker process to communicate over specified UNIX socker file
firecracker = subprocess.Popen([firecracker_path, '--api-sock', socket_path],
                                 stdin=subprocess.PIPE, stdout=sys.stdout,
                                 stderr=subprocess.STDOUT)

# Prepare arguments we are going to pass when creating VM instance
dirname = os.path.dirname(os.path.abspath(__file__))
kernel_path = os.path.join(dirname, '../build/release/loader-stripped.elf')

if len(sys.argv) > 1:
    cmdline = sys.argv[1]
else:
    with open(os.path.join(dirname, '../build/release/cmdline'), 'r') as f:
        cmdline = f.read()

# Create API client and make API calls
client = ApiClient(socket_path.replace("/", "%2F"))

try:
    # Very often on the very first run firecracker process
    # is not ready yet to accept calls over socket file
    # so we poll existence of this file as an good
    # enough indicator of firecracker readyness
    while not os.path.exists(socket_path):
        time.sleep(0.01)
    print_time("Firecracker ready")

    client.create_instance(kernel_path, cmdline)
    print_time("Created OSv VM")

    client.start_instance()
    print_time("Booted OSv VM")
except Exception as e:
    print("Failed to run OSv on firecracker due to: ({0}): {1} !!!".format(e.errno, e.strerror))
    firecracker.kill()
    exit(-1)

print_time("Waiting for firecracker process to terminate")
firecracker.wait()
print_time("End")
