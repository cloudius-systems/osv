#!/usr/bin/python2

import json
import sys
import os
import subprocess

src_path="../../.."
build_path=".."

def read_config():
	with open(os.path.join(src_path, "config.json")) as file:
		return json.load(file)

def fetch_module(module, target_dir):
	print "Fetching %s" % module["name"]

	if module["type"] == "git":
		cmd = "git clone -b %s %s %s" % (module["branch"], module["path"], target_dir)
	elif module["type"] == "svn":
		cmd = "svn co %s %s" % (module["path"], target_dir)
	elif module["type"] == "dir":
		cmd = "cp -a %s %s" % (module["path"], target_dir)
	else:
		raise Exception("%s is unknown type" % module["type"])

	print cmd
	subprocess.call([cmd], shell=True)

def append_lines(file_path, dst_file):
	with open(file_path) as src_file:
		for line in src_file:
			dst_file.write(line)

def get_module_dir(module):
	return os.path.join(build_path, "module", module["name"])

def get_selected_modules(config):
	return config["modules"]

def generate_manifests():
	for manifest_type in ["usr", "bootfs"]:
		manifest_name = "%s.manifest" % manifest_type
		print "Preparing %s" % manifest_name

		with open(os.path.join(build_path, manifest_name), "w") as manifest:
			append_lines(os.path.join(src_path, "%s.skel" % manifest_name), manifest)

			for module in get_selected_modules(config):
				module_manifest = os.path.join(get_module_dir(module), manifest_name)
				print "Appending %s to %s" % (module_manifest, manifest_name)
				append_lines(module_manifest, manifest)

def make_modules():
	for module in get_selected_modules(config):
		module_dir = get_module_dir(module)

		if not os.path.exists(module_dir):
			fetch_module(module, module_dir)
			subprocess.call(["make module"], shell=True, cwd=module_dir)

if __name__ == "__main__":
	config = read_config()

	make_modules()
	generate_manifests()
