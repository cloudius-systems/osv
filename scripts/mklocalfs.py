#!/usr/bin/python

import os, optparse, io, subprocess, socket, threading, stat, sys, shutil
try:
    import configparser
except ImportError:
    import ConfigParser as configparser

try:
    import StringIO
    # This works on Python 2
    StringIO = StringIO.StringIO
except ImportError:
    # This works on Python 3
    StringIO = io.StringIO

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

def upload(out, manifest):
    files = dict([(f, manifest.get('manifest', f, vars=defines))
                  for f in manifest.options('manifest')])

    files = list(expand(files.items()))
    files = [(x, unsymlink(y)) for (x, y) in files]

    for name, hostname in files:
        localname = out + name
        if hostname.startswith('->'): # Ignore links for the time being
            os.symlink(hostname[2:], localname)
            print "skipping %s" % hostname
        else:
            if os.path.isdir(hostname):
                if not os.path.exists(localname):
                    os.makedirs(localname)
            elif os.path.isfile(hostname):
                dirname = os.path.abspath(os.path.join(localname, os.pardir))
                if not os.path.exists(dirname):
                    os.makedirs(dirname)
                shutil.copyfile(hostname, localname)
                print "adding %s" % name


def main():
    make_option = optparse.make_option

    opt = optparse.OptionParser(option_list=[
            make_option('-o',
                        dest='output',
                        help='write to FILE',
                        metavar='FILE'),
            make_option('-m',
                        dest='manifest',
                        help='read manifest from FILE',
                        metavar='FILE'),
            make_option('-D',
                        type='string',
                        help='define VAR=DATA',
                        metavar='VAR=DATA',
                        action='callback',
                        callback=add_var),
    ])

    (options, args) = opt.parse_args()

    depends = StringIO()
    manifest = configparser.SafeConfigParser()
    manifest.optionxform = str # avoid lowercasing
    manifest.read(options.manifest)

    output_folder = os.path.abspath(options.output)
    upload(output_folder, manifest)

    depends.write('\n\n')
    depends.close()

if __name__ == "__main__":
    main()
