#!/usr/bin/python

import re
import os
import subprocess
import operator
import argparse
from functools import reduce
from osv.modules import api, resolve, filemap

class isolated_jvm(api.basic_app):
    multimain_manifest = '/etc/javamains'
    apps = []

    def prepare_manifest(self, build_dir, manifest_type, manifest):
        if manifest_type != 'usr':
            return

        javamains_path = os.path.join(build_dir, 'javamains')
        with open(javamains_path, "w") as mains:
            for app in self.apps:
                mains.write('\n'.join(app.get_multimain_lines()) + '\n')

        manifest.write('%s: %s\n' % (self.multimain_manifest, javamains_path))

    def get_launcher_args(self):
        jvm_args = []
        for app in self.apps:
            jvm_args.extend(app.get_jvm_args())

        return ['java.so'] + jvm_args + ['io.osv.isolated.MultiJarLoader', '-mains', self.multimain_manifest]

    def add(self, app):
        self.apps.append(app)

    def has_any_app(self):
        return self.apps

class non_isolated_jvm(api.basic_app):
    app = None

    def get_launcher_args(self):
        return ['java.so'] + self.app.get_jvm_args() + self.app.get_multimain_lines()

    def add(self, app):
        self.app = app

    def has_any_app(self):
        return self.app

def expand(text, variables):
    def resolve(m):
        name = m.group('name')
        if not name in variables:
            raise Exception('Undefined variable: ' + name)
        return variables[name]

    return re.sub(r'\${(?P<name>.*)}', resolve, text)

def append_manifest(file_path, dst_file, variables={}):
    with open(file_path) as src_file:
        for line in src_file:
            line = line.rstrip()
            if line != '[manifest]':
                dst_file.write(expand(line + '\n', variables))

def generate_manifests(modules, basic_apps, usrskel='default'):
    for manifest_type in ["usr", "bootfs"]:
        manifest_name = "%s.manifest" % manifest_type
        print("Preparing %s" % manifest_name)

        manifest_skel = "%s.skel" % manifest_name
        if manifest_type == "usr" and usrskel != "default":
            manifest_skel = usrskel

        with open(os.path.join(resolve.get_build_path(), manifest_name), "w") as manifest:
            manifest.write('[manifest]\n')

            if manifest_skel != 'none':
                append_manifest(os.path.join(resolve.get_osv_base(), manifest_skel), manifest)

            for module in modules:
                module_manifest = os.path.join(module.local_path, manifest_name)

                if os.path.exists(module_manifest):
                    print("Appending %s to %s" % (module_manifest, manifest_name))
                    append_manifest(module_manifest, manifest, variables={
                        'MODULE_DIR': module.local_path,
                        'OSV_BASE': resolve.get_osv_base()
                    })

                filemap_attr = '%s_files' % manifest_type
                if hasattr(module, filemap_attr):
                    filemap.as_manifest(getattr(module, filemap_attr), manifest.write)

            for app in basic_apps:
                app.prepare_manifest(resolve.get_build_path(), manifest_type, manifest)

def format_args(args):
    if isinstance(args, str):
        return args
    else:
        return ' '.join(args)

def get_command_line(basic_apps):
    args = [format_args(app.get_launcher_args()) for app in basic_apps]
    return '&'.join((a for a in args if a))

def make_cmd(cmdline, j, jobserver):
    ret = 'make ' + cmdline
    if jobserver is not None:
        ret += ' -j --jobserver-fds=' + jobserver
    elif j is '-':
        ret += ' -j'
    elif j is not None:
        ret += ' -j' + j
    return ret

def make_modules(modules, args):
    for module in modules:
        if os.path.exists(os.path.join(module.local_path, 'Makefile')):
            if subprocess.call(make_cmd('module', j=args.j, jobserver=args.jobserver_fds),
                               shell=True, cwd=module.local_path):
                raise Exception('make failed for ' + module.name)

def flatten_list(elememnts):
    if not elememnts:
        return []
    if not isinstance(elememnts, list):
        return [elememnts]
    return reduce(operator.add, [flatten_list(e) for e in elememnts])

def get_basic_apps(apps):
    basic_apps = []
    java = resolve.require('java-base')
    if hasattr(java,'non_isolated_jvm') and java.non_isolated_jvm:
        _jvm = non_isolated_jvm()
    else:
        _jvm = isolated_jvm()

    for app in flatten_list(apps):
        if isinstance(app, api.basic_app):
            basic_apps.append(app)
        elif isinstance(app, api.java_app):
            _jvm.add(app)
        else:
            raise Exception("Unknown app type: " + str(app))

    if _jvm.has_any_app():
        basic_apps.append(_jvm)

    return basic_apps

def generate_cmdline(apps):
    cmdline_path = os.path.join(resolve.get_build_path(), "cmdline")
    print("Saving command line to %s" % cmdline_path)
    with open(cmdline_path, "w") as cmdline_file:
        if apps:
            cmdline_file.write(get_command_line(apps))
        else:
            print("No apps selected")

def build(args):
    add_default = True
    if args.image_config[0] == "!":
        add_default = False
        args.image_config = args.image_config[1:]
    image_config_file = os.path.join(image_configs_dir, args.image_config + '.py')
    if os.path.exists(image_config_file):
        print("Using image config: %s" % image_config_file)
        config = resolve.local_import(image_config_file)
        run_list = config.get('run', [])
        selected_modules = []
    else:
        # If images/image_config doesn't exist, assume image_config is a
        # comma-separated list of module names, and build an image from those
        # modules (and their dependencies). The command line to run is to
        # run each of the module's "default" command line, in parallel.
        # You can choose a module's non-default command line, with a dot,
        # e.g.: mgmt.shell use's mgmt's "shell" command line.
        print("No such image configuration: " + args.image_config + ". Assuming list of modules.")
        run_list = []
        disabled_modules = set()
        selected_modules = args.image_config.split(",")
        for module in selected_modules:
            if module[0] == '-':
                disabled_modules.add(module[1:])
        module_names = []
        config = resolve.read_config()
        if add_default and "default" in config:
            module_names += config["default"]
        module_names += selected_modules
        module_names = [i for i in module_names if not i in disabled_modules]
        for missing in list(disabled_modules - set(module_names)):
             raise Exception("Attempt to disable module %s but not enabled" % missing)

        for module in module_names:
            if module[0] == '-':
                continue
            a = module.split(".", 1)
            name = a[0]
            if len(a) > 1:
                api.require_running(name, a[1])
            else:
                api.require_running(name)

        # Add modules that are implicitly required if others are present
        resolve.resolve_required_modules_if_other_is_present()

    # By default append manifests from all modules resolved through api.require()
    # otherwise (add_required_to_manifest=False) only append manifests from the selected_modules
    if args.add_required_to_manifest:
        modules = resolve.get_required_modules()
    else:
        modules = list(module for module in resolve.get_required_modules() if module.name in selected_modules)

    modules_to_run = resolve.get_modules_to_run()

    print("Modules:")
    if not modules:
        print("  None")
    for module in modules:
        prefix = "  " + module.name
        if module in modules_to_run:
            print(prefix + "." + modules_to_run[module])
        else:
            print(prefix)

    for module, run_config_name in modules_to_run.items():
        run_config = resolve.get_run_config(module, run_config_name)
        if run_config:
            run_list.append(run_config)

    make_modules(modules, args)

    apps_to_run = get_basic_apps(run_list)
    generate_manifests(modules, apps_to_run, args.usrskel)
    generate_cmdline(apps_to_run)

def clean(args):
    extra_args = {}
    if args.quiet:
        extra_args['stdout'] = open('/dev/null', 'w')

    for local_path in resolve.all_module_directories():
        if os.path.exists(os.path.join(local_path, 'Makefile')):
            if not args.quiet:
                print('Cleaning ' + local_path + ' ...')
            if subprocess.call(make_cmd('-q clean', j=args.j, jobserver=args.jobserver_fds),
                               shell=True, cwd=local_path, stderr=subprocess.PIPE, **extra_args) != 2:
                if subprocess.call(make_cmd('clean', j=args.j, jobserver=args.jobserver_fds),
                                   shell=True, cwd=local_path, **extra_args):
                    raise Exception('\'make clean\' failed in ' + local_path)

if __name__ == "__main__":
    image_configs_dir = resolve.get_images_dir()

    parser = argparse.ArgumentParser(prog='module.py')
    parser.add_argument('--jobserver-fds', action='store', default=None,
                        help='make -j support')
    parser.add_argument('-j', action='store', default=None,
                        help='make -j support')
    subparsers = parser.add_subparsers(help="Command")

    build_cmd = subparsers.add_parser("build", help="Build modules")
    build_cmd.add_argument("-c", "--image-config", action="store", default="default",
                        help="image configuration name. Looked up in " + image_configs_dir)
    build_cmd.add_argument("--usrskel", action="store", default="default",
                        help="override default usr.manifest.skel")
    build_cmd.add_argument("--no-required", dest="add_required_to_manifest", action="store_false",
                           help="do not add files to usr.manifest from modules implicitly resolved through api.require()")
    build_cmd.set_defaults(func=build,add_required_to_manifest=True)

    clean_cmd = subparsers.add_parser("clean", help="Clean modules")
    clean_cmd.add_argument("-q", "--quiet", action="store_true")
    clean_cmd.set_defaults(func=clean)

    args = parser.parse_args()
    if 'j' not in args:
        args.jobserver_fds = None
    args.func(args)
