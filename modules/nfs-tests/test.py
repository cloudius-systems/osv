#!/usr/bin/env python3

import time
import tempfile
import atexit
from tests.testing import *

def make_export_and_conf():
    export_dir = tempfile.mkdtemp(prefix='share')
    os.chmod(export_dir, 0o777)
    (conf_fd, conf_path) = tempfile.mkstemp(prefix='export')
    conf = os.fdopen(conf_fd, "w")
    conf.write("%s 127.0.0.1(insecure,rw)\n" % export_dir)
    conf.flush()
    conf.close()
    return (conf_path, export_dir)

proc = None
conf_path = None
export_dir = None

def kill_unfsd():
    global proc, conf_path, export_dir
    proc.kill()
    proc.wait()
    if conf_path and os.path.exists(conf_path):
        os.unlink(conf_path)
    if export_dir and os.path.exists(export_dir):
        import shutil
        shutil.rmtree(export_dir, ignore_errors=True)

dirname = os.path.dirname(os.path.abspath(__file__))
UNFSD = dirname + "/unfsd.bin"

def run_test():
    global proc, conf_path, export_dir
    start = time.time()

    if not os.path.exists(UNFSD):
        print("Please do:\n\tmake nfs-server")
        sys.exit(1)

    (conf_path, export_dir) = make_export_and_conf()

    ret = subprocess.call(['rpcinfo'])
    if ret != 0:
        print('Please install rpcbind!')
        exit(-1)

    proc = subprocess.Popen([os.path.join(os.getcwd(), UNFSD),
                             "-t",
                             "-d",
                             "-s",
                             "-l", "127.0.0.1",
                             "-e", conf_path ],
                             stdin = sys.stdin,
                             stdout = subprocess.PIPE,
                             stderr = sys.stderr,
                             shell = False)
    atexit.register(kill_unfsd)
    test = SingleCommandTest('nfs-test',
        "/tst-nfs.so --server 192.168.122.1 --share %s" %
        export_dir)

    line = proc.stdout.readline().decode()
    while line:
         print(line)
         if "/tmp" in line:
            break
         line = proc.stdout.readline().decode()

    sys.stdout.write("NFS Test \n")
    sys.stdout.write("Shared directory: [%s]\n" % export_dir)
    sys.stdout.flush()

    try:
        test.run()
    except:
        sys.stdout.write("NFS Test FAILED\n")
        sys.stdout.flush()
        raise
    end = time.time()

    duration = end - start
    sys.stdout.write("OK  (%.3f s)\n" % duration)
    sys.stdout.flush()
    kill_unfsd()

set_verbose_output(True)
run_test()
