#!/usr/bin/env python
import collections
import fnmatch
import random
import os
import runpy
import argparse
import time
#import remote
import subprocess
import pprint
import json
import time
import re
#import wrkparse
import traceback
#import supervision
import shutil
import sys
from random import randrange
from contextlib import contextmanager
#from json_utils import *
from string import Template
from os import listdir
from os.path import isfile, join

class ConfigTemplate(Template):
    delimiter = '$$'
    idpattern = r'[a-z][\.\-_a-z0-9]*'
#
# config files
#
def config_merge(a, b, path=None):
    if path is None: path = []
    for key in b:
        if key in a:
            if isinstance(a[key], dict) and isinstance(b[key], dict):
                config_merge(a[key], b[key], path + [str(key)])
            elif a[key] == b[key]:
                pass # same leaf value
            else:
                template = ConfigTemplate(b[key])
                val = template.substitute(a)
                a[key] = val
        else:
            a[key] = b[key]
    return a

def config_flatten(d, parent_key='', sep='.'):
    items = []
    for k, v in d.items():
        new_key = parent_key + sep + k if parent_key else k
        if isinstance(v, collections.MutableMapping):
            items.extend(config_flatten(v, new_key).items())
        else:
            items.append((new_key, v))
    return dict(items)

# TODO need to update the method config files are found - currently seraching up the path
def config(dir, params):
    # find config files
    config_files = []
    old_dir = ""
    while old_dir != dir:
       old_dir = dir
       if os.path.isfile(os.path.join(dir, "test-config.json")):
          config_files.insert(0, os.path.join(dir, "test-config.json"))
       dir = os.path.abspath(os.path.join(dir, ".."))
    result = ""
    # merge config files
    for fconfig in config_files:
       new_config = json.load(open(fconfig))
       if result == "":
          result = new_config
       else:
          config_merge(result, new_config)
    # flatten config
    config = config_flatten(result)
    # apply params
    missing_key = False
    for key in config:
        template = ConfigTemplate(config[key])
        try:
           val = template.substitute(params)
           config[key] = val
        except KeyError:
           print "missing value for config value", key
           missing_key = True
    if missing_key:
       sys.exit(1)
    return config

#
# execution templates processing
#

def get_templates_from_list(list):
    return fnmatch.filter(list, "*.template.*")

def get_file_from_template(template):
    return template.replace(".template", "")

def template_compare_filename(item1, item2):
    item1_val = int(item1.split("_")[1])
    item2_val = int(item2.split("_")[1])
    if item1_val < item2_val:
        return -1
    elif item1_val > item2_val:
        return 1
    else:
        return 0

def template_prepare(params, dir):
    for (dirpath, dirnames, filenames) in os.walk(dir):
        for filename in get_templates_from_list(filenames):
            template_apply(os.path.join(dirpath, filename), params)
        for dir in fnmatch.filter(dirnames, "[a-zA-Z0-9].*"):
            template_prepare(params, dir)

def template_apply(in_file, params):
    out_file = get_file_from_template(in_file)
    in_fh = open(in_file, 'r')
    template = ConfigTemplate(in_fh.read())
    out_fh = open(out_file, 'w')
    try:
       out_fh.write(template.substitute(params))
    except KeyError:
       print "missing value for config value in", in_file
       missing_key = True
       sys.exit(1)
    in_fh.close()
    out_fh.close()
    os.chmod(out_file, 0755)
    print "compiling template", in_file, "->", out_file

#
# run logic
#
def run_file(file):
    print "running ", file
    file_stdout = open(file + ".stdout", "w")
    file_stderr = open(file + ".stderr", "w")
    file_return = subprocess.call(file, stdout=file_stdout, stderr=file_stderr, shell=True)
    file_stdout.close()
    file_stderr.close();
    print "return ", file_return
    if file_return != 0:
       file_stderr = open(file + ".stderr", "r")
       print file_stderr.read()
    return file_return


def extract_config_params_from_args(args):
    args_dictionary = vars(args)
    params = {}
    if args_dictionary['config_param'] != None:
       for key_val in args_dictionary['config_param']:
           key_vals = key_val.split(":")
           params[key_vals[0]] = key_vals[1]
    return params;


def run(args):
    compile(args)
    error = False
    for dir in args.directory:
        print "running files in", dir
        files = []
        for (dirpath, dirnames, filenames) in os.walk(dir):
            for filename in get_templates_from_list(filenames):
                files.append(filename)
        files.sort(cmp=template_compare_filename)
        for file in files:
            file_return = run_file(os.path.join(dir, get_file_from_template(file)))
            if file_return != 0:
               error = True
               break
        if error:
           break;
    if error:
        sys.exit(1)

def compile(args):
    params = extract_config_params_from_args(args)
    for dir in args.directory:
        print "compiling files in", dir
        configuration = config(dir, params)
        template_prepare(configuration, dir)


if __name__ == "__main__":
    parser = argparse.ArgumentParser('Tester')
    subparsers = parser.add_subparsers(help="command")

    _run = subparsers.add_parser('run')
    _run.add_argument('directory', nargs='+', help='directory to run tests')
    _run.add_argument('--config_param', action='append', help='config param to be passed')
    _run.set_defaults(func=run)

    _compile = subparsers.add_parser('compile')
    _compile.add_argument('directory', nargs='+', help='directory to compile tests')
    _compile.add_argument('--config_param', action='append', help='config param to be passed')
    _compile.set_defaults(func=compile)

    args = parser.parse_args()
    args.func(args)
