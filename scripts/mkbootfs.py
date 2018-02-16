#!/usr/bin/python

import os, struct, optparse, io
try:
    import configparser
except ImportError:
    import ConfigParser as configparser
from manifest_common import add_var, expand, unsymlink, read_manifest, defines, strip_file

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
    files = [(x, y % defines) for (x, y) in files]
    files = list(expand(files))
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
