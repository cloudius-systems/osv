import os
import sys
import json
import re
import subprocess

_required_modules = []

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
    file_name = os.path.basename(path)
    m = re.match(r'(.*)\.py', file_name)
    if not m:
        raise Exception("Invalid module file: " + file_name)

    py_module_name = m.group(1);
    module_dir = os.path.dirname(path)

    sys.path.insert(0, module_dir)
    py_module = __import__(py_module_name)
    sys.path.remove(module_dir)
    del sys.modules[py_module_name]
    return py_module

def get_required_modules():
    return _required_modules

def is_direct(module):
    return module["type"] == "direct-dir"

def get_module_dir(module):
    if is_direct(module):
        return module["path"]
    return os.path.join(get_build_path(), "module", module["name"])

def find_module_descriptor(module_name):
    config = read_config()

    if "include" in config["modules"]:
        for f in config["modules"]["include"]:
            with open(os.path.expandvars(f)) as file:
                config["modules"].update(json.load(file))

    if not module_name in config["modules"]:
        return None

    desc = config["modules"][module_name]

    if not "name" in desc:
        desc["name"] = module_name

    desc["path"] = os.path.expandvars(desc["path"])

    return desc

def fetch_module(module, target_dir):
    print "Fetching %s" % module["name"]

    module_type = module["type"]
    if module_type == "git":
        cmd = "git clone -b %s %s %s" % (module["branch"], module["path"], target_dir)
    elif module_type == "svn":
        cmd = "svn co %s %s" % (module["path"], target_dir)
    elif module_type == "dir":
        cmd = "cp -a %s %s" % (module["path"], target_dir)
    elif module_type == "direct-dir":
        raise Exception("Trying to fetch direct module")
    else:
        raise Exception("%s is unknown type" % module_type)

    print cmd
    returncode = subprocess.call([cmd], shell=True)
    if returncode:
        raise Exception("Command failed with exit code: %d" % returncode)

def require(module_name):
    desc = find_module_descriptor(module_name)
    if not desc:
        raise Exception("Module not found: %s. Please check configuration: %s" % (module_name, get_config_path()))

    _required_modules.append(desc)

    module_dir = get_module_dir(desc)
    if not os.path.exists(module_dir):
        if is_direct(desc):
            raise Exception("Path does not exist: " + module_dir)
        fetch_module(desc, module_dir)

    py_module_file = 'module.py'
    module_config_file = os.path.join(module_dir, py_module_file)
    if not os.path.exists(module_config_file):
        print "No %s in %s" % (py_module_file, module_dir)
        return {}

    print "Importing %s" % module_config_file
    return local_import(module_config_file)
