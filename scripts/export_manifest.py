#!/usr/bin/python3

import optparse, os, shutil
from manifest_common import add_var, expand, unsymlink, read_manifest, defines, strip_file

# This will export the package based on the provided manifest file. It uses the same mechanism to
# get the files that need copying as the actual upload process. The only current limitation is
# support for links in OSv, e.g., /etc/mnttab: ->/proc/mounts.
def export_package(manifest, dest):
    abs_dest = os.path.abspath(dest)
    print("[INFO] exporting into directory %s" % abs_dest)

    # Remove and create the base directory where we are going to put all package files.
    if os.path.exists(abs_dest):
        shutil.rmtree(abs_dest)
    os.makedirs(abs_dest)

    files = list(expand(manifest))
    files = [(x, unsymlink(y % defines)) for (x, y) in files]

    host_symlinks = []

    for name, hostname in files:
        name = name[1:] if name.startswith("/") else name
        name = os.path.join(abs_dest, name)

        if hostname.startswith("->"):
            link_source = hostname[2:]
            target_dir = os.path.dirname(name)

            if link_source.startswith("/"):
                link_source = os.path.join(abs_dest, link_source[1:])
            else:
                link_source = os.path.abspath(os.path.join(target_dir, link_source))

            link_source = os.path.relpath(link_source, target_dir)

            if not os.path.exists(target_dir):
                os.makedirs(target_dir)

            os.symlink(link_source, name)
            print("[INFO] added link %s -> %s" % (name, link_source))

        else:
            # If it is a symlink, then resolve it add to the list of host symlinks to be created later
            if os.path.islink(hostname):
                link_source = os.readlink(hostname)
                host_symlinks.append((link_source,name))
            # If it is a file, copy it to the target directory.
            elif os.path.isfile(hostname):
                # Make sure the target dir exists
                dirname = os.path.dirname(name)
                if not os.path.exists(dirname):
                    os.makedirs(dirname)

                if hostname.endswith("-stripped.so"):
                    continue
                hostname = strip_file(hostname)

                shutil.copy(hostname, name)
                print("[INFO] exported %s" % name)
            elif os.path.isdir(hostname):
                # If hostname is a dir, it is only a request to create the folder on guest. Nothing to copy.
                if not os.path.exists(name):
                    os.makedirs(name)
                print("[INFO] created dir %s" % name)

            else:
                # Inform the user that the rule cannot be applied. For example, this happens for links in OSv.
                print("[ERR] unable to export %s" % hostname)

    for link_source, name in host_symlinks:
        target_dir = os.path.dirname(name)
        if not os.path.exists(target_dir):
            os.makedirs(target_dir)
        os.symlink(link_source, name)
        print("[INFO] added link %s -> %s" % (name, link_source))

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
                        callback=add_var),
            make_option('-e',
                        dest='export',
                        help='exports the contents of the usr.manifest into a given folder (which is deleted first)',
                        metavar='FILE'),
    ])

    (options, args) = opt.parse_args()

    manifest = read_manifest(options.manifest)
    export_package(manifest, options.export)

if __name__ == "__main__":
    main()
