#!/usr/bin/python3

import os, io, re, subprocess

defines = {}

def add_var(option, opt, value, parser):
    var, val = value.split('=')
    defines[var] = val

def expand(items):
    for name, hostname in items:
        if name.endswith('/**') and hostname.endswith('/**'):
            name = name[:-2]
            hostname = hostname[:-2]
            for dirpath, dirnames, filenames in os.walk(hostname):
                for filename in filenames:
                    relpath = dirpath[len(hostname):]
                    if relpath != "":
                        relpath += "/"
                    yield (name + relpath + filename,
                           hostname + relpath + filename)
        elif '/&/' in name and hostname.endswith('/&'):
            prefix, suffix = name.split('/&/', 1)
            yield (prefix + '/' + suffix, hostname[:-1] + suffix)
        else:
            yield (name, hostname)

def unsymlink(f):
    if f.startswith('!'):
        return f[1:]
    if f.startswith('->'):
        return f
    try:
        link = os.readlink(f)
        if link.startswith('/'):
            # try to find a match
            base = os.path.dirname(f)
            while not os.path.exists(base + link):
                if base == '/':
                    return f
                base = os.path.dirname(base)
        else:
            base = os.path.dirname(f) + '/'
        return unsymlink(base + link)
    except Exception:
        return f

# Reads the manifest and returns it as a list of pairs (guestpath, hostpath).
def read_manifest(fn):
    ret = []
    comment = re.compile("^[ \t]*(|#.*|\[manifest])$")
    with open(fn, 'r') as f:
        for line in f:
            line = line.rstrip();
            if comment.match(line): continue
            components = line.split(": ", 2)
            guestpath = components[0].strip();
            hostpath = components[1].strip()
            ret.append((guestpath, hostpath))
    return ret

def strip_file(filename):
    def to_strip(filename):
        ff = os.path.abspath(filename)
        osvdir = os.path.abspath('../..')
        return ff.startswith(os.getcwd()) or \
               ff.startswith(osvdir + "/modules") or \
               ff.startswith(osvdir + "/apps")

    stripped_filename = filename
    if filename.endswith(".so") and to_strip(filename):
        stripped_filename = filename[:-3] + "-stripped.so"
        if not os.path.exists(stripped_filename) \
                or (os.path.getmtime(stripped_filename) < \
                            os.path.getmtime(filename)):
            if os.environ.get('STRIP'):
                strip_cmd = os.environ.get('STRIP')
            else:
                strip_cmd = 'strip'
            ret = subprocess.call([strip_cmd, "-o", stripped_filename, filename])
            if ret != 0:
                print("Failed stripping %s. Using original." % filename)
                return filename
    return stripped_filename
