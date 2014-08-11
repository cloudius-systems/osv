#!/usr/bin/env python
import time
import basetest

class testos(basetest.Basetest):
    def test_os_version(self):
        path = self.path_by_nick(self.os_api, "getOSversion")
        self.assertRegexpMatches(self.curl(path), r"v0\.\d+(-rc\d+)?(-\d+-[0-9a-z]+)?" , path)

    def test_manufactor(self):
        self.validate_path(self.os_api, "getOSmanufacturer", "cloudius-systems")

    def test_os_uptime(self):
        path = self.path_by_nick(self.os_api, "getLastBootUpTime")
        up_time = self.curl(path)
        time.sleep(2)
        self.assert_between(path, up_time + 1, up_time + 3, self.curl(path))

    def test_os_date(self):
        path = self.path_by_nick(self.os_api, "getDate")
        val = self.curl(path).encode('ascii', 'ignore')
        self.assertRegexpMatches(val, "...\\s+...\\s+\\d+\\s+\\d\\d:\\d\\d:\\d\\d\\s+20..", path)

    def test_os_total_memory(self):
        path = self.path_by_nick(self.os_api, "getTotalVirtualMemorySize")
        val = self.curl(path)
        self.assertGreater(val, 1024 * 1024 * 256, msg="Memory should be greater than 256Mb")

    def test_os_free_memory(self):
        path = self.path_by_nick(self.os_api, "getFreeVirtualMemory")
        val = self.curl(path)
        self.assertGreater(val, 1024 * 1024 * 256, msg="Free memory should be greater than 256Mb")

    def test_os_threads(self):
        path = self.path_by_nick(self.os_api, "listThreads")
        val = self.curl(path)
        self.assert_key_in("time_ms", val)
        ctime = val["time_ms"]
        idle_thread = next((item for item in val["list"] if item["name"] == "idle1"), None)
        idle = idle_thread["cpu_ms"]
        id = idle_thread["id"]
        cpu = idle_thread["cpu"]
        self.assertEqual(cpu,1)
        time.sleep(2)
        val = self.curl(path)
        self.assert_key_in("time_ms", val)
        self.assert_between(path, ctime + 1000, ctime + 3000, val["time_ms"])
        idle_thread = next((item for item in val["list"] if item["name"] == "idle1"), None)
        idle1 = idle_thread["cpu_ms"]
        self.assert_between(path + " idle thread cputime was" + str(idle)+
                            " new time=" + str(idle1), idle + 1000, idle + 3000, idle1)
        self.assertEqual(id, idle_thread["id"])
