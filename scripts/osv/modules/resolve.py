import os
import sys
import json
import re
import subprocess
import runpy

_modules = dict()
_loading_modules = list()

class Module(object):
    def __init__(self, name, config, properties):
        self.name = name
        self.local_path = _get_module_dir(config)
        self.properties = properties

    def __getattr__(self, name):
        try:
            return self.properties[name]
        except KeyError:
            raise AttributeError(name)

def get_osv_base():
    return os.environ['OSV_BASE']

def get_build_path():
    return os.environ['OSV_BUILD_PATH']

def get_images_dir():
    return os.path.join(get_osv_base(), "images")

def get_config_path():
    return os.path.join(get_osv_base(), "config.json")

def read_config():
    with open(get_config_path()) as file:
        return json.load(file)

def local_import(path):
    return runpy.run_path(path)

def get_required_modules():
    return _modules.values()

def _is_direct(module_config):
    return module_config["type"] == "direct-dir"

def _get_module_dir(module_config):
    if _is_direct(module_config):
        return module_config["path"]
    return os.path.join(get_build_path(), "module", module_config["name"])

def find_module_config(module_name):
    config = read_config()

    if "include" in config["modules"]:
        for f in config["modules"]["include"]:
            with open(os.path.expandvars(f)) as file:
                config["modules"].update(json.load(file))

    if not module_name in config["modules"]:
        return None

    module_config = config["modules"][module_name]
    module_config["path"] = os.path.expandvars(module_config["path"])
    return module_config

def fetch_module(module_config, target_dir):
    print "Fetching %s" % module_config["name"]

    module_type = module_config["type"]
    if module_type == "git":
        cmd = "git clone -b %s %s %s" % (module_config["branch"], module_config["path"], target_dir)
    elif module_type == "svn":
        cmd = "svn co %s %s" % (module_config["path"], target_dir)
    elif module_type == "dir":
        cmd = "cp -a %s %s" % (module_config["path"], target_dir)
    elif module_type == "direct-dir":
        raise Exception("Trying to fetch direct module")
    else:
        raise Exception("%s is unknown type" % module_type)

    print cmd
    returncode = subprocess.call([cmd], shell=True)
    if returncode:
        raise Exception("Command failed with exit code: %d" % returncode)

def require(module_name):
    if module_name in _loading_modules:
        raise Exception("Recursive loading of '%s' module" % module_name)

    module = _modules.get(module_name, None)
    if module:
        return module

    module_config = find_module_config(module_name)
    if not module_config:
        raise Exception("Module not found: %s. Please check configuration: %s" % (module_name, get_config_path()))

    module_dir = _get_module_dir(module_config)
    if not os.path.exists(module_dir):
        if _is_direct(module_config):
            raise Exception("Path does not exist: " + module_dir)
        fetch_module(module_config, module_dir)

    py_module_file = 'module.py'
    module_file = os.path.join(module_dir, py_module_file)
    if not os.path.exists(module_file):
        print "No %s in %s" % (py_module_file, module_dir)
        module_properties = {}
    else:
        _loading_modules.append(module_name)
        try:
            print "Importing %s" % module_file
            module_properties = local_import(module_file)
        finally:
            _loading_modules.remove(module_name)

    module = Module(module_name, module_config, module_properties)
    _modules[module_name] = module
    return module
