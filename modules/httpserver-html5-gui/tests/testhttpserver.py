#!/usr/bin/env python
import sys
import argparse
import os
import unittest
import basetest

from osv import client

parser = argparse.ArgumentParser(description="""Testing the Httpserver""")

module_base = os.path.join(os.path.realpath(os.path.dirname(__file__)), '..')
osv_base = os.path.join(module_base, '..', '..')

parser.add_argument('--connect', help='Connect to an existing image', action='store_true')
parser.add_argument('--run_script', help='path to the run image script', default=os.path.join(osv_base, 'scripts', 'run.py'))
parser.add_argument('--cmd', help='the command to execute')
parser.add_argument('--use_sudo', help='Use sudo with -n option instead of port forwarding', action='store_true')
parser.add_argument('--jsondir', help='location of the json files', default=os.path.join(module_base, '../httpserver-api/api-doc/listings/'))
client.Client.add_arguments(parser)

if __name__ == '__main__':
    basetest.Basetest.set_config(parser)
    basetest.Basetest.config.check_jvm = False
    basetest.Basetest.start_image()
    del sys.argv[1:]
    tests = unittest.TestLoader().discover(os.path.join(module_base, 'tests', 'static'), pattern='*.py')
    test_suite = unittest.TestSuite(tests)
    unittest.TextTestRunner(verbosity=2).run(test_suite)
    basetest.Basetest.shutdown()
