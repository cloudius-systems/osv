#!/usr/bin/env python
import json
import sys
import re
import glob
import argparse
import os
import subprocess
import time
import threading
import urllib2
import urllib
import unittest
import re
import socket
import basetest

parser = argparse.ArgumentParser(description="""Testing the Httpserver""")

module_base = os.path.join(os.path.realpath(os.path.dirname(__file__)), '..')
osv_base = os.path.join(module_base, '..', '..')

parser.add_argument('--connect', help='Connect to an existing image', action='store_true')
parser.add_argument('--run_script', help='path to the run image script', default=os.path.join(osv_base, 'scripts', 'run.py'))
parser.add_argument('--cmd', help='the command to execute')
parser.add_argument('--use_sudo', help='Use sudo with -n option instead of port forwarding', action='store_true')
parser.add_argument('--jsondir', help='location of the json files', default=os.path.join(module_base, 'api-doc/listings/'))
parser.add_argument('--port', help='set the port number', type=int,
                    default=8000)
parser.add_argument('--ip', help='set the ip address',
                    default='localhost')


class test_httpserver(basetest.Basetest):
    @classmethod
    def setUpClass(cls):
        basetest.Basetest.start_image()

    @classmethod
    def tearDownClass(cls):
        basetest.Basetest.shutdown()

if __name__ == '__main__':
    basetest.Basetest.set_config(parser)
    basetest.Basetest.start_image()
    del sys.argv[1:]
    testsuite = unittest.TestLoader().discover('modules/httpserver/tests/api', pattern='*.py')
    unittest.TextTestRunner(verbosity=2).run(testsuite)
    basetest.Basetest.shutdown()
