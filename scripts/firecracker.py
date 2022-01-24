#!/usr/bin/env python3
#
# pip install requests-unixsocket
import sys
import os
import stat
import json
import subprocess
import signal
import time
import argparse
import re
import json
import tempfile
from datetime import datetime

verbose = False

stty_params = None

devnull = open('/dev/null', 'w')

osv_base = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')

host_arch = os.uname().machine

def stty_save():
    global stty_params
    p = subprocess.Popen(["stty", "-g"], stdout=subprocess.PIPE, stderr=devnull)
    stty_params, err = p.communicate()
    stty_params = stty_params.strip()

def stty_restore():
    if stty_params:
        subprocess.call(["stty", stty_params], stderr=devnull)

class ApiException(Exception):
    pass


class ApiClient(object):
    def __init__(self, domain_socket_path = None):
        if domain_socket_path != None:
           import requests_unixsocket
           self.socket_less = False
           self.socket_path = domain_socket_path
           self.session = requests_unixsocket.Session()
        else:
           self.socket_less = True
           self.firecracker_config = {}
        print_time("API socket-less: %s" % self.socket_less)

    def api_socket_url(self, path):
        return "http+unix://%s%s" % (self.socket_path, path)

    def make_put_call(self, path, request_body):
        url = self.api_socket_url(path)
        res = self.session.put(url, data=json.dumps(request_body))
        if res.status_code != 204:
            raise ApiException(res.text)
        return res.status_code

    def create_instance(self, kernel_image_path, cmdline):
        boot_source = {
            'kernel_image_path': kernel_image_path,
            'boot_args': cmdline
        }
        if self.socket_less:
            self.firecracker_config['boot-source'] = boot_source
        else:
            self.make_put_call('/boot-source', boot_source)

    def add_disk(self, disk_image_path):
        drive = {
            'drive_id': 'rootfs',
            'path_on_host': disk_image_path,
            'is_root_device': False,
            'is_read_only': False
        }
        if self.socket_less:
            self.firecracker_config['drives'] = [drive]
        else:
            self.make_put_call('/drives/rootfs', drive)

    def add_network_interface(self, interface_name, host_interface_name):
        interface = {
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
        }
        if self.socket_less:
            self.firecracker_config['network-interfaces'] = [interface]
        else:
            self.make_put_call('/network-interfaces/%s' % interface_name, interface)

    def start_instance(self):
        if self.socket_less == False:
            self.make_put_call('/actions', {
                'action_type': 'InstanceStart'
            })

    def configure_logging(self):
        log_config = {
            "log_fifo": "log.fifo",
            "metrics_fifo": "metrics.fifo",
            "level": "Info",
            "show_level": True,
            "show_log_origin": True
        }
        if self.socket_less:
            self.firecracker_config['logger'] = log_config
        else:
            self.make_put_call('/logger', log_config)

    def configure_machine(self, vcpu_count, mem_size_in_mb):
        machine_config = {
            'vcpu_count': vcpu_count,
            'mem_size_mib': mem_size_in_mb,
            'ht_enabled' : False
        }
        if self.socket_less:
            self.firecracker_config['machine-config'] = machine_config
        else:
            self.make_put_call('/machine-config', machine_config)

    def firecracker_config_json(self):
        return json.dumps(self.firecracker_config, indent=3)

def print_time(msg):
    if verbose:
        now = datetime.now()
        print("%s: %s" % (now.isoformat(), msg))


def setup_tap_interface(mode, tap_interface_name, tap_ip=None, physical_nic=None, bridge_name=None):
    # Setup tun tap interface if does not exist
    # sudo ip link delete fc_tap0 - this deletes the tap device
    tuntap_interfaces = subprocess.check_output(['ip', 'tuntap']).decode('utf-8')
    if tuntap_interfaces.find(tap_interface_name) < 0:
        print("The tap interface %s not found -> needs to set it up!" % tap_interface_name)
        dirname = os.path.dirname(os.path.abspath(__file__))
        setup_networking_script = os.path.join(dirname, 'setup_fc_networking.sh')
        # Check if the bridge exists if user specified it
        if mode == 'bridged' and bridge_name:
            bridges = subprocess.check_output(['brctl', 'show']).decode('utf-8')
            if bridges.find(bridge_name) < 0:
                print("The bridge %s does not exist per brctl. Please create one!" % bridge_name)
                exit(-1)
            print("Setting up TAP device in bridged mode!")
            subprocess.call([setup_networking_script, 'bridged', tap_interface_name, bridge_name])
        else:
            print("Setting up TAP device in natted mode!")
            if physical_nic is not None:
                subprocess.call([setup_networking_script, 'natted', tap_interface_name, tap_ip, physical_nic])
            else:
                subprocess.call([setup_networking_script, 'natted', tap_interface_name, tap_ip])

def find_firecracker(dirname, arch):
    firecracker_path = os.path.join(dirname, '../.firecracker/firecracker-%s' % arch)
    if os.environ.get('FIRECRACKER_PATH'):
        firecracker_path = os.environ.get('FIRECRACKER_PATH')

    # And offer to install if not found
    firecracker_version = 'v0.23.0'
    if not os.path.exists(firecracker_path):
        url_base = 'https://github.com/firecracker-microvm/firecracker/releases/download'
        download_url = '%s/%s/firecracker-%s-%s' % (url_base, firecracker_version, firecracker_version, arch)
        answer = input("Firecracker executable has not been found under %s. "
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
    stty_save()
    return subprocess.Popen([firecracker_path, '--api-sock', socket_path],
                           stdout=sys.stdout, stderr=subprocess.STDOUT)

def start_firecracker_with_no_api(firecracker_path, firecracker_config_json):
    #  Start firecracker process and pass configuration JSON as a file
    api_file = tempfile.NamedTemporaryFile(delete=False)
    api_file.write(bytes(firecracker_config_json, 'utf-8'))
    api_file.flush()
    stty_save()
    return subprocess.Popen([firecracker_path, "--no-api", "--config-file", api_file.name],
                           stdout=sys.stdout, stderr=subprocess.STDOUT), api_file.name


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
    firecracker_path = find_firecracker(dirname, options.arch)

    # Firecracker is installed so lets start
    print_time("Start")
    socket_path = '/tmp/firecracker.socket'
    if options.api:
        firecracker = start_firecracker(firecracker_path, socket_path)

    # Prepare arguments we are going to pass when creating VM instance
    raw_disk_path = disk_path(options.image_path)

    cmdline = options.execute
    if not cmdline:
        with open(os.path.join(dirname, '../build/last/cmdline'), 'r') as f:
            cmdline = f.read()

    if options.arch == 'aarch64':
        cmdline = "console=tty --nopci %s" % cmdline
    else:
        cmdline = "--nopci %s" % cmdline

    if options.networking:
        tap_device = 'fc_tap0'
        if not options.bridge:
            tap_ip = '172.16.0.1'
            client_ip = '172.16.0.2'
            cmdline = '--ip=eth0,%s,255.255.255.252 --defaultgw=%s --nameserver=%s %s' % (client_ip, tap_ip, tap_ip, cmdline)
            setup_tap_interface('natted', tap_device, tap_ip, options.physical_nic)
        else:
            setup_tap_interface('bridged', tap_device, None, None, options.bridge)

    if options.verbose:
        cmdline = '--verbose ' + cmdline

    # Create API client and make API calls
    if options.api:
        client = ApiClient(socket_path.replace("/", "%2F"))
    else:
        client = ApiClient()

    try:
        # Very often on the very first run firecracker process
        # is not ready yet to accept calls over socket file
        # so we poll existence of this file as a good
        # enough indicator if firecracker is ready
        if options.api:
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

        client.create_instance(options.kernel_path, cmdline)
        print_time("Created OSv VM with cmdline: %s" % cmdline)

        if not options.api:
            if options.verbose:
                print(client.firecracker_config_json())
            firecracker, config_file_path = start_firecracker_with_no_api(firecracker_path, client.firecracker_config_json())
        else:
            client.start_instance()
            print_time("Booted OSv VM")

    except ApiException as e:
        print("Failed to make firecracker API call: %s." % e)
        firecracker.kill()
        stty_restore()
        exit(-1)

    except Exception as e:
        print("Failed to run OSv on firecracker due to: ({0}): {1} !!!".format(e.errno, e.strerror))
        firecracker.kill()
        stty_restore()
        exit(-1)

    print_time("Waiting for firecracker process to terminate")
    try:
        firecracker.wait()
    except KeyboardInterrupt:
        os.kill(firecracker.pid, signal.SIGINT)

    stty_restore()

    if not options.api:
        os.unlink(config_file_path)
    print_time("End")


if __name__ == "__main__":
    # Parse arguments
    parser = argparse.ArgumentParser(prog='firecracker')
    parser.add_argument("-d", "--debug", action="store_true",
                        help="start debug version")
    parser.add_argument("-r", "--release", action="store_true",
                        help="start release version")
    parser.add_argument("-c", "--vcpus", action="store", type=int, default=1,
                        help="specify number of vcpus")
    parser.add_argument("-m", "--memsize", action="store", default="128M",
                        help="specify memory: ex. 1G, 2G, ...")
    parser.add_argument("-e", "--execute", action="store", default=None, metavar="CMD",
                        help="overwrite command line")
    parser.add_argument("-i", "--image", action="store", default=None, metavar="CMD",
                        help="path to disk image file. defaults to ../build/last/usr.img")
    parser.add_argument("-k", "--kernel", action="store", default=None, metavar="CMD",
                        help="path to kernel loader file. defaults to ../build/last/kernel.elf")
    parser.add_argument("-n", "--networking", action="store_true",
                        help="needs root to setup tap networking first time")
    parser.add_argument("-b", "--bridge", action="store", default=None,
                        help="bridge name for tap networking")
    parser.add_argument("-V", "--verbose", action="store_true",
                        help="pass --verbose to OSv, to display more debugging information on the console")
    parser.add_argument("-a", "--api", action="store_true",
                        help="use socket-based API to configure and start OSv on firecracker")
    parser.add_argument("-p", "--physical_nic", action="store", default=None,
                        help="name of the physical NIC (wired or wireless) to forward to if in natted mode")
    parser.add_argument("--arch", action="store", choices=["x86_64","aarch64"], default=host_arch,
                        help="specify Firecracker architecture: x86_64, aarch64")

    cmdargs = parser.parse_args()
    cmdargs.opt_path = "debug" if cmdargs.debug else "release" if cmdargs.release else "last"
    if cmdargs.arch == 'aarch64':
        default_kernel_file_name = "loader.img"
        default_image_file_name = "disk.img"
    else:
        default_kernel_file_name = "kernel.elf"
        default_image_file_name = "usr.img"
    cmdargs.kernel_path = os.path.abspath(cmdargs.kernel or os.path.join(osv_base, "build/%s/%s" % (cmdargs.opt_path, default_kernel_file_name)))
    cmdargs.image_path = os.path.abspath(cmdargs.image or os.path.join(osv_base, "build/%s/%s" % (cmdargs.opt_path, default_image_file_name)))
    if cmdargs.verbose:
        verbose = True
    main(cmdargs)
