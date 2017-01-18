import os
import json
import subprocess
import runpy
import collections

_modules = collections.OrderedDict()
_loading_modules = list()
_modules_to_run = dict()
_modules_to_be_added_if_other_module_present = dict()

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
        config = json.load(file)

        if "include" in config["modules"]:
            for f in config["modules"]["include"]:
                with open(os.path.expandvars(f)) as file:
                    config["modules"].update(json.load(file))

        return config

def local_import(path):
    return runpy.run_path(path)

def unique(items):
    seen = set()
    return (x for x in items if not x in seen and not seen.add(x))

def get_required_modules():
    """
    Returns a list of modules in inverse topological order
    according to dependency graph

    """
    return list(unique(_modules.values()))

def _is_direct(module_config):
    return module_config["type"] == "direct-dir"

def _get_module_dir(module_config):
    if _is_direct(module_config):
        return module_config["path"]
    return os.path.join(get_build_path(), "module", module_config["name"])

def find_module_config(module_name):
    config = read_config()

    if module_name in config["modules"]:
        module_config = config["modules"][module_name]
        module_config["path"] = os.path.expandvars(module_config["path"])
        return module_config

    if "repositories" in config["modules"]:
        for repo_path in config["modules"]["repositories"]:
            module_path = os.path.join(os.path.expandvars(repo_path), module_name)
            if os.path.exists(module_path):
                return {
                    'path': module_path,
                    'type': 'direct-dir'
                }

def all_module_directories():
    config = read_config()

    for module_name, module_config in config["modules"].items():
        if module_name == "repositories":
            for repo in config["modules"]["repositories"]:
                repo_path = os.path.expandvars(repo)
                for module_name in os.listdir(repo_path):
                    module_path = os.path.join(repo_path, module_name)
                    if os.path.isdir(module_path):
                        yield module_path
        elif module_config['type'] == 'direct-dir':
            yield os.path.expandvars(module_config["path"])

def fetch_module(module_config, target_dir):
    print("Fetching %s" % module_config["name"])

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

    print(cmd)
    returncode = subprocess.call([cmd], shell=True)
    if returncode:
        raise Exception("Command failed with exit code: %d" % returncode)

def require_if_other_module_present(module_name,other_module_name):
    list_of_modules = _modules_to_be_added_if_other_module_present.get(other_module_name,None)
    if(list_of_modules):
        list_of_modules.append(module_name)
    else:
        _modules_to_be_added_if_other_module_present[other_module_name] = [module_name]

def resolve_required_modules_if_other_is_present():
    required_module_names = set()
    for module_name in _modules_to_be_added_if_other_module_present.keys():
        # If module is present then add modules that should be required implictly
        if( _modules.get(module_name)):
            modules_to_be_added = _modules_to_be_added_if_other_module_present[module_name]
            for required_module_name in modules_to_be_added:
                if(not _modules.get(required_module_name)): # If required module is not in the list already
                    print("Adding module '%s' because module '%s' is present" % (required_module_name, module_name))
                    required_module_names.add(required_module_name)

    for required_module_name in required_module_names:
        require(required_module_name)

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
        print("No %s in %s" % (py_module_file, module_dir))
        module_properties = {}
    else:
        _loading_modules.append(module_name)
        try:
            print("Importing %s" % module_file)
            module_properties = local_import(module_file)
        finally:
            _loading_modules.remove(module_name)

    module = Module(module_name, module_config, module_properties)
    _modules[module_name] = module
    if hasattr(module, 'provides'):
        for name in getattr(module, 'provides'):
            if(_modules.get(name)):
                raise Exception("There is more than one module included that provides: '%s'" % name )
            else:
                _modules[name] = module
    return module

def require_running(module_name, run_config='*'):
    """
    run_config holds the name of required run configuration. There are two values
    with special meaning:

      '*'    wildcard represents any run configuration. If not overriden by more
             strict requirements will yield the 'default' run configuration.

      'none' represents an empty run configuration (nothing is run)

    """
    module = require(module_name)
    old_config = _modules_to_run.get(module, None)
    if old_config:
        if old_config == '*':
            _modules_to_run[module] = run_config
        elif run_config != '*' and old_config != run_config:
            raise Exception('Desired to run %s.%s but .%s already selected' % (module_name, run_config, old_config))
    else:
        _modules_to_run[module] = run_config

def get_run_config(module, run_config):
    if run_config == 'none':
        return None

    if run_config == '*':
        attr_name = 'default'
    else:
        attr_name = run_config

    if hasattr(module, attr_name):
        return getattr(module, attr_name)
    elif run_config != '*':
        raise Exception("Attribute %s not set in module %s" % (attr_name, module.name))

def get_modules_to_run():
    return _modules_to_run
