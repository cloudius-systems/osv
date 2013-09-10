#!/bin/env python
import socket
from Queue import Queue
from threading import Thread
import sys

class Worker(Thread):
    """Thread executing tasks from a given tasks queue"""
    def __init__(self, tasks):
        Thread.__init__(self)
        self.tasks = tasks
        self.daemon = True
        self.start()
    
    def run(self):
        while True:
            func, args, kargs = self.tasks.get()
            try: func(*args, **kargs)
            except Exception, e: print e
            self.tasks.task_done()

class ThreadPool:
    """Pool of threads consuming tasks from a queue"""
    def __init__(self, num_threads):
        self.tasks = Queue(num_threads)
        for _ in range(num_threads): Worker(self.tasks)

    def add_task(self, func, *args, **kargs):
        """Add a task to the queue"""
        self.tasks.put((func, args, kargs))

    def wait_completion(self):
        """Wait for completion of all the tasks in the queue"""
        self.tasks.join()

def hash_function(data):
    result = 0
    for x in data:
        result ^= ord(x)
    return result

def make_connection():
    global data
    global drops
    global hash_errors
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("192.168.122.89", 2500))
        data = "".join(data)    
        s.send(data)
        s.send("END")
        res = s.recv(1)
        s.close()
        if (ord(res[0]) != expected):
            hash_errors = hash_errors + 1
        
    except:
        drops = drops + 1
    
if __name__ == "__main__":

    nthreads = 0
    connections = 0

    try:
        nthreads = int(sys.argv[1])
        connections = int(sys.argv[2])
    except:
        print "Usage: ./tst-tcp-hash-cli.py <nthreads> <connections>"
        sys.exit()

    #data = range(0, (4096**2)*2, 11)
    data = range(0,4096, 11)
    data = map(lambda x: chr(x % 256), data)
    expected = hash_function(data)

    print "Sending %d bytes requests, expected hash: %d" % (len(data), expected)
    print "Creating %d threads and making %d connections, please wait..." % (nthreads, connections)

    drops = 0
    hash_errors = 0

    pool = ThreadPool(nthreads)
    for i in range(connections):
        pool.add_task(make_connection)

    pool.wait_completion()

    # FIXME: these metrics may not be accurate as I didn't use locks and interfere with the test
    print "Test completed with %d drops and %d hash errors" % (drops, hash_errors)


