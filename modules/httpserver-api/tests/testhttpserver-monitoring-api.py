#!/usr/bin/env python3
import sys
import argparse
import os
import unittest
import basetest

from osv import client

parser = argparse.ArgumentParser(description="""Testing the read-only Httpserver""")

module_base = os.path.join(os.path.realpath(os.path.dirname(__file__)), '..')
osv_base = os.path.join(module_base, '..', '..')

parser.add_argument('--connect', help='Connect to an existing image', action='store_true')
parser.add_argument('--run_script', help='path to the run image script', default=os.path.join(osv_base, 'scripts', 'run.py'))
parser.add_argument('--cmd', help='the command to execute')
parser.add_argument('--use_sudo', help='Use sudo with -n option instead of port forwarding', action='store_true')
parser.add_argument('--jsondir', help='location of the json files', default=os.path.join(module_base, 'api-doc/listings/'))
parser.add_argument('--test_image', help='the path to the test image')
parser.add_argument('--hypervisor', action="store", default="qemu", help="choose hypervisor to run: qemu, firecracker")
client.Client.add_arguments(parser)

class test_httpserver(basetest.Basetest):
    @classmethod
    def setUpClass(cls):
        basetest.Basetest.start_image()

    @classmethod
    def tearDownClass(cls):
        basetest.Basetest.hard_shutdown()

if __name__ == '__main__':
    basetest.Basetest.set_config(parser)
    basetest.Basetest.config.check_jvm = False
    basetest.Basetest.start_image()
    del sys.argv[1:]
    api_tests = unittest.TestLoader().discover(os.path.join(module_base, 'tests', 'monitoring-api'), pattern='*.py')
    test_suite = unittest.TestSuite((api_tests))
    unittest.TextTestRunner(verbosity=2).run(test_suite)
    basetest.Basetest.hard_shutdown()
