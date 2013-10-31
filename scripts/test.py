#!/usr/bin/env python2
import subprocess

tests = [
    "/tests/tst-mmap.so",
    "/tests/tst-mmap-file.so",
    "/tests/tst-rename.so",
    "/tests/tst-vfs.so",
    "/tests/tst-stat.so",
]

def run_test(name):
    args = ["-g", "-e", name]
    subprocess.call(["./scripts/run.py"] + args)

def run_tests():
    for test in tests:
        run_test(test)

def main():
    run_tests()

if (__name__ == "__main__"):
    main()
