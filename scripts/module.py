#!/usr/bin/python2

import json
import sys
import os
import subprocess

f = open("../../../config.json")
conf = json.load(f)
f.close()

mtype = sys.argv[1]
mskel = open("../../../%s.manifest.skel" % mtype)
m = open("../%s.manifest" % mtype, "w")
for l in mskel.readlines():
	m.write(l)
mskel.close()

for mod in conf["modules"]:
	mmod_path = "%s/%s.manifest" % (mod["name"], mtype)
	if os.access(mod["name"], os.F_OK) == False:
		if mod["type"] == "git":
			cmd = "git clone -b %s %s %s" % (mod["branch"], mod["path"], mod["name"])
		elif mod["type"] == "svn":
			cmd = "svn co %s %s" % (mod["path"], mod["name"])
		elif mod["type"] == "dir":
			cmd = "cp -a %s %s" % (mod["path"], mod["name"])
		else:
			raise Exception("%s is unknown type" % mod["type"])
		print cmd
		subprocess.call([cmd], shell=True)
	if os.access(mmod_path, os.F_OK) == False:
		pwd = os.getcwd()
		print "cd %s" % mod["name"]
		os.chdir(mod["name"])
		cmd = "make module"
		subprocess.call([cmd], shell=True)
		print "cd -"
		os.chdir(pwd)
	print "append %s to %s.manifest" % (mmod_path, mtype)
	mmod = open(mmod_path)
	for l in mmod.readlines():
		m.write(l)
	mmod.close()
m.close()
