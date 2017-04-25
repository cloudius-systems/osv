#!/usr/bin/python

import os, struct, optparse, io, subprocess
try:
    import configparser
except ImportError:
    import ConfigParser as configparser

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
    try:
        link = os.readlink(f)
        if link.startswith('/'):
            # try to find a match
            base = os.path.dirname(f)
            while not os.path.exists(base + link):
                base = os.path.dirname(base)
        else:
            base = os.path.dirname(f) + '/'
        return unsymlink(base + link)
    except Exception:
        return f

def read_manifest(fn):
    manifest = configparser.SafeConfigParser()
    manifest.optionxform = str # avoid lowercasing
    manifest.read(fn)

    files = dict([(f, manifest.get('manifest', f, vars=defines))
                  for f in manifest.options('manifest')])
    return files

def to_strip(filename):
    ff = os.path.abspath(filename)
    osvdir = os.path.abspath('../..')
    return ff.startswith(os.getcwd()) or \
        ff.startswith(osvdir + "/modules") or \
        ff.startswith(osvdir + "/apps")

def strip_file(filename):
    stripped_filename = filename
    if filename.endswith(".so") and to_strip(filename):
        stripped_filename = filename[:-3] + "-stripped.so"
        if not os.path.exists(stripped_filename) \
                or (os.path.getmtime(stripped_filename) < \
                    os.path.getmtime(filename)):
            subprocess.call([os.getenv("STRIP", "strip"), "-o", stripped_filename, filename])
    return stripped_filename

def main():
    make_option = optparse.make_option

    opt = optparse.OptionParser(option_list=[
            make_option('-o',
                        dest='output',
                        help='write to FILE',
                        metavar='FILE'),
            make_option('-d',
                        dest='depends',
                        help='write dependencies to FILE',
                        metavar='FILE',
                        default=None),
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

    # See unpack_bootfs() as the reference to this ad-hoc packing format.
    metadata_size = 128
    depends = io.StringIO()
    if options.depends:
        depends = open(options.depends, 'w')
    out = open(options.output, 'wb')
    
    depends.write(u'%s: \\\n' % (options.output,))

    files = read_manifest(options.manifest)
    files = list(expand(files.items()))
    files = [(x, unsymlink(y)) for (x, y) in files]
    files = [(x, y) for (x, y) in files if not x.endswith("-stripped.so")]
    files = [(x, strip_file(y)) for (x, y) in files]

    pos = (len(files) + 1) * metadata_size

    for name, hostname in files:
        type = 0
        if hostname.startswith("->"):
            link = hostname[2:]
            type = 1
            size = len(link.encode())+1
        elif os.path.isdir(hostname):
            size = 0;
            if not name.endswith("/"):
                name += "/"
        else:
            size = os.stat(hostname).st_size

        # FIXME: check if name.encode()'s length is more than 110 (111
        # minus the necessary null byte) and fail if it is.
        metadata = struct.pack('QQb111s', size, pos, type, name.encode())
        out.write(metadata)
        pos += size
        depends.write(u'\t%s \\\n' % (hostname,))

    out.write(struct.pack('128s', b''))

    for name, hostname in files:
        if os.path.isdir(hostname):
            continue
        if hostname.startswith("->"):
            link = hostname[2:]
            out.write(link.encode())
            out.write('\0')
        else:
            out.write(open(hostname, 'rb').read())

    depends.write(u'\n\n')

    out.close()
    depends.close()

if __name__ == "__main__":
    main()
