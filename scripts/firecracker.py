#!/usr/bin/env python
#
# pip install requests-unixsocket
import sys
import os
import stat
import json
import subprocess
import time
import argparse
import re
from datetime import datetime
import requests_unixsocket


verbose = False


class ApiException(Exception):
    pass


class ApiClient(object):
    def __init__(self, domain_socket_path):
        self.socket_path = domain_socket_path
        self.session = requests_unixsocket.Session()

    def api_socket_url(self, path):
        return "http+unix://%s%s" % (self.socket_path, path)

    def make_put_call(self, path, request_body):
        url = self.api_socket_url(path)
        res = self.session.put(url, data=json.dumps(request_body))
        if res.status_code != 204:
            raise ApiException(res.text)
        return res.status_code

    def create_instance(self, kernel_image_path, cmdline):
        self.make_put_call('/boot-source', {
            'kernel_image_path': kernel_image_path,
            'boot_args': cmdline
        })

    def add_disk(self, disk_image_path):
        self.make_put_call('/drives/rootfs', {
            'drive_id': 'rootfs',
            'path_on_host': disk_image_path,
            'is_root_device': False,
            'is_read_only': False
        })

    def add_network_interface(self, interface_name, host_interface_name, ):
        self.make_put_call('/network-interfaces/%s' % interface_name, {
            'iface_id': interface_name,
            'host_dev_name': host_interface_name,
            'guest_mac': "52:54:00:12:34:56",
            'rx_rate_limiter': {
               'bandwidth': {
                  'size': 0,
                  'refill_time': 0
               },
               'ops': {
                  'size': 0,
                  'refill_time': 0
               }
            },
            'tx_rate_limiter': {
               'bandwidth': {
                  'size': 0,
                  'refill_time': 0
               },
               'ops': {
                  'size': 0,
                  'refill_time': 0
               }
            }
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

    def configure_machine(self, vcpu_count, mem_size_in_mb):
        self.make_put_call('/machine-config', {
            'vcpu_count': vcpu_count,
            'mem_size_mib': mem_size_in_mb,
            'ht_enabled' : False
        })


def print_time(msg):
    if verbose:
        now = datetime.now()
        print("%s: %s" % (now.isoformat(), msg))


def setup_tap_interface(tap_interface_name, tap_ip, bridge_name):
    # Setup tun tap interface if does not exist
    # sudo ip link delete fc_tap0 - this deletes the tap device
    tuntap_interfaces = subprocess.check_output(['ip', 'tuntap'])
    if tuntap_interfaces.find(tap_interface_name) < 0:
        print("The tap interface %s not found -> needs to set it up!" % tap_interface_name)
        # Check if the bridge exists if user specified it
        if bridge_name:
            bridges = subprocess.check_output(['brctl', 'show'])
            if bridges.find(bridge_name) < 0:
                print("The bridge %s does not exist per brctl. Please create one!" % bridge_name)
                exit(-1)

        subprocess.call(['sudo', 'ip', 'tuntap', 'add', 'dev', tap_interface_name, 'mode', 'tap'])
        subprocess.call(['sudo', 'sysctl', '-q', '-w', 'net.ipv4.conf.%s.proxy_arp=1' % tap_interface_name])
        subprocess.call(['sudo', 'sysctl', '-q', '-w', 'net.ipv6.conf.%s.disable_ipv6=1' % tap_interface_name])
        subprocess.call(['sudo', 'ip', 'link', 'set', 'dev', tap_interface_name, 'up'])

        if bridge_name:
            subprocess.call(['sudo', 'brctl', 'addif', bridge_name, tap_interface_name])
        else:
            subprocess.call(['sudo', 'ip', 'addr', 'add', '%s/30' % tap_ip, 'dev', tap_interface_name])


def find_firecracker(dirname):
    firecracker_path = os.path.join(dirname, '../.firecracker/firecracker')
    if os.environ.get('FIRECRACKER_PATH'):
        firecracker_path = os.environ.get('FIRECRACKER_PATH')

    # And offer to install if not found
    firecracker_version = 'v0.18.0'
    if not os.path.exists(firecracker_path):
        url_base = 'https://github.com/firecracker-microvm/firecracker/releases/download'
        download_url = '%s/%s/firecracker-%s' % (url_base, firecracker_version, firecracker_version)
        answer = raw_input("Firecracker executable has not been found under %s. "
                           "Would you like to download it from %s and place it under %s? [y|n]" %
                           (firecracker_path, download_url, firecracker_path))
        if answer.capitalize() != 'Y':
            print("Firecracker not available. Exiting ...")
            sys.exit(-1)

        directory = os.path.dirname(firecracker_path)
        if not os.path.exists(directory):
            os.mkdir(directory)
        download_path = firecracker_path + '.download'
        ret = subprocess.call(['wget', download_url, '-O', download_path])
        if ret != 0:
            print('Failed to download %s!' % download_url)
            exit(-1)

        subprocess.call(["strip", "-o", firecracker_path, download_path])
        os.chmod(firecracker_path, stat.S_IRUSR | stat.S_IXUSR)
        os.unlink(download_path)

    return firecracker_path


def disk_path(qcow_disk_path):
    dot_pos = qcow_disk_path.rfind('.')
    raw_disk_path = qcow_disk_path[0:dot_pos] + '.raw'

    # Firecracker is not able to use disk image files in QCOW format
    # so we have to convert usr.img to raw format if the raw disk is missing
    # or source qcow file is newer
    if not os.path.exists(raw_disk_path) or os.path.getctime(qcow_disk_path) > os.path.getctime(raw_disk_path):
        ret = subprocess.call(['qemu-img', 'convert', '-O', 'raw', qcow_disk_path, raw_disk_path])
        if ret != 0:
            print('Failed to convert %s to a raw format %s!' % (qcow_disk_path, raw_disk_path))
            exit(-1)
    return raw_disk_path


def start_firecracker(firecracker_path, socket_path):
    # Delete socket file if exists
    if os.path.exists(socket_path):
        os.unlink(socket_path)

    # Start firecracker process to communicate over specified UNIX socket file
    return subprocess.Popen([firecracker_path, '--api-sock', socket_path],
                           stdout=sys.stdout, stderr=subprocess.STDOUT)


def get_memory_size_in_mb(options):
    memory_in_mb = 128
    if options.memsize:
        regex = re.search('(\d+[MG])', options.memsize)
        if len(regex.groups()) > 0:
            mem_size = regex.group(1)
            memory_in_mb = int(mem_size[:-1])
            if mem_size.endswith('G'):
                memory_in_mb = memory_in_mb * 1024
    return memory_in_mb


def main(options):
    # Check if firecracker is installed
    dirname = os.path.dirname(os.path.abspath(__file__))
    firecracker_path = find_firecracker(dirname)

    # Firecracker is installed so lets start
    print_time("Start")
    socket_path = '/tmp/firecracker.socket'
    firecracker = start_firecracker(firecracker_path, socket_path)

    # Prepare arguments we are going to pass when creating VM instance
    kernel_path = options.kernel
    if not kernel_path:
       kernel_path = os.path.join(dirname, '../build/release/loader-stripped.elf')

    qemu_disk_path = options.image
    if not qemu_disk_path:
       qemu_disk_path = os.path.join(dirname, '../build/release/usr.img')
    raw_disk_path = disk_path(qemu_disk_path)

    cmdline = options.execute
    if not cmdline:
        with open(os.path.join(dirname, '../build/release/cmdline'), 'r') as f:
            cmdline = f.read()
    cmdline = "--nopci %s" % cmdline

    if options.networking:
        tap_ip = '172.16.0.1'
        setup_tap_interface('fc_tap0', tap_ip, options.bridge)
        if not options.bridge:
            client_ip = '172.16.0.2'
            cmdline = '--ip=eth0,%s,255.255.255.252 --defaultgw=%s %s' % (client_ip, tap_ip, cmdline)

    if options.verbose:
        cmdline = '--verbose ' + cmdline

    # Create API client and make API calls
    client = ApiClient(socket_path.replace("/", "%2F"))

    try:
        # Very often on the very first run firecracker process
        # is not ready yet to accept calls over socket file
        # so we poll existence of this file as a good
        # enough indicator if firecracker is ready
        while not os.path.exists(socket_path):
            time.sleep(0.01)
        print_time("Firecracker ready")

        memory_in_mb = get_memory_size_in_mb(options)
        client.configure_machine(options.vcpus, memory_in_mb)
        print_time("Configured VM")

        client.add_disk(raw_disk_path)
        print_time("Added disk")

        if options.networking:
            client.add_network_interface('eth0', 'fc_tap0')

        client.create_instance(kernel_path, cmdline)
        print_time("Created OSv VM with cmdline: %s" % cmdline)

        client.start_instance()
        print_time("Booted OSv VM")

    except ApiException as e:
        print("Failed to make firecracker API call: %s." % e)
        firecracker.kill()
        exit(-1)

    except Exception as e:
        print("Failed to run OSv on firecracker due to: ({0}): {1} !!!".format(e.errno, e.strerror))
        firecracker.kill()
        exit(-1)

    print_time("Waiting for firecracker process to terminate")
    firecracker.wait()
    print_time("End")


if __name__ == "__main__":
    # Parse arguments
    parser = argparse.ArgumentParser(prog='firecracker')
    parser.add_argument("-c", "--vcpus", action="store", type=int, default=1,
                        help="specify number of vcpus")
    parser.add_argument("-m", "--memsize", action="store", default="128M",
                        help="specify memory: ex. 1G, 2G, ...")
    parser.add_argument("-e", "--execute", action="store", default=None, metavar="CMD",
                        help="overwrite command line")
    parser.add_argument("-i", "--image", action="store", default=None, metavar="CMD",
                        help="path to disk image file. defaults to ../build/release/usr.img")
    parser.add_argument("-k", "--kernel", action="store", default=None, metavar="CMD",
                        help="path to kernel loader file. defaults to ../build/release/loader-stripped.elf")
    parser.add_argument("-n", "--networking", action="store_true",
                        help="needs root to setup tap networking first time")
    parser.add_argument("-b", "--bridge", action="store", default=None,
                        help="bridge name for tap networking")
    parser.add_argument("-V", "--verbose", action="store_true",
                        help="pass --verbose to OSv, to display more debugging information on the console")

    cmd_args = parser.parse_args()
    if cmd_args.verbose:
        verbose = True
    main(cmd_args)
