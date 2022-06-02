#!/usr/bin/python3

import optparse, os, sys, subprocess
from manifest_common import add_var, expand, unsymlink, read_manifest, defines

def list_files(manifest,manifest_dir):
    manifest = [(x, y % defines) for (x, y) in manifest]
    files = list(expand(manifest))
    files = [(x, unsymlink(y)) for (x, y) in files]

    for name, hostname in files:
        if not hostname.startswith("->"):
            if os.path.islink(hostname):
                link = os.readlink(hostname)
                print(link)
            elif not os.path.isdir(hostname):
                if not os.path.isabs(hostname):
                    hostname = os.path.join(manifest_dir,hostname)
                print(hostname)

def main():
    make_option = optparse.make_option

    opt = optparse.OptionParser(option_list=[
            make_option('-m',
                        dest='manifest',
                        help='read manifest from FILE',
                        metavar='FILE'),
            make_option('-D',
                        type='string',
                        help='define VAR=DATA',
                        metavar='VAR=DATA',
                        action='callback',
                        callback=add_var)
    ])

    (options, args) = opt.parse_args()

    if not 'libgcc_s_dir' in defines:
        libgcc_s_path = subprocess.check_output(['gcc', '-print-file-name=libgcc_s.so.1']).decode('utf-8')
        defines['libgcc_s_dir'] = os.path.dirname(libgcc_s_path)

    manifest_path = options.manifest or 'build/last/usr.manifest'
    manifest_dir = os.path.abspath(os.path.dirname(manifest_path))

    manifest = read_manifest(manifest_path)
    list_files(manifest,manifest_dir)

if __name__ == "__main__":
    main()
