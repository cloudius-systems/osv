#!/bin/env python
import socket
from Queue import Queue
from threading import Thread

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
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("192.168.122.89", 2500))
    data = "".join(data)    
    s.send(data)
    s.send("END")
    res = s.recv(1)
    s.close()
    print 'Received', ord(res[0])
    
if __name__ == "__main__":

    #data = range(0, (4096**2)*2, 11)
    data = range(0,4096, 11)
    data = map(lambda x: chr(x % 256), data)
    print "Expecting:", hash_function(data)

    pool = ThreadPool(200)
    for i in range(200):
        pool.add_task(make_connection)

    pool.wait_completion()

