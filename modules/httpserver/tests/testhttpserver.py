#!/usr/bin/env python
import json
import sys
import re
import glob
import argparse
import os
import subprocess
import time
import threading
import urllib2
import urllib
import unittest
import re
import socket

parser = argparse.ArgumentParser(description="""Testing the Httpserver""")

module_base = os.path.join(os.path.realpath(os.path.dirname(__file__)), '..')
osv_base = os.path.join(module_base, '..', '..')

parser.add_argument('--connect', help='Connect to an existing image', action='store_true')
parser.add_argument('--run_script', help='path to the run image script', default=os.path.join(osv_base, 'scripts', 'run.py'))
parser.add_argument('--cmd', help='the command to execute',
                    default="/usr/mgmt/httpserver.so&"
                    "java.so io.osv.MultiJarLoader -mains /etc/javamains")
parser.add_argument('--use_sudo', help='Use sudo with -n option instead of port forwarding', action='store_true')
parser.add_argument('--jsondir', help='location of the json files', default=os.path.join(module_base, 'api-doc/listings/'))
parser.add_argument('--port', help='set the port number', type=int,
                    default=8000)
parser.add_argument('--ip', help='set the ip address',
                    default='localhost')

config = parser.parse_args()

class test_httpserver(unittest.TestCase):
    @classmethod
    def get_url(cls, uri):
        return 'http://' + config.ip + ':' + str(config.port) + uri

    @classmethod
    def get_json_api(cls, name):
        json_data = open(os.path.join(config.jsondir, name))
        data = json.load(json_data)
        json_data.close()
        return data

    def assert_between(self, msg, low, high, val):
        self.assertGreaterEqual(val, low, msg=msg)
        self.assertLessEqual(val, high, msg=msg)

    def assert_key_in(self, key, dic):
        self.assertTrue(key in dic, key + " not found in dictionary " + json.dumps(dic))

    @classmethod
    def get_api(cls, api_definition, nickname):
        for api in api_definition["apis"]:
            if api["operations"][0]["nickname"] == nickname:
                return api
        return None

    @classmethod
    def path_by_nick(cls, api_definition, nickname):
        api = cls.get_api(api_definition, nickname)
        return api["path"]
    
    @classmethod
    def is_jvm_up(cls):
         return cls.curl(cls.path_by_nick(cls.jvm_api, "getJavaVersion")) != ""

    @classmethod
    def is_reachable(cls):
        s = socket.socket()
        try:
            s.connect((config.ip, config.port))
            s.close()
            return cls.is_jvm_up()
        except socket.error:
            return False

    def validate_path(self, api_definition, nickname, value):
        path = self.path_by_nick(api_definition, nickname)
        self.assertEqual(value, self.curl(path))

    def validate_path_regex(self, api_definition, nickname, expr):
        path = self.path_by_nick(api_definition, nickname)
        self.assertRegexpMatches(self.curl(path), expr)

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

    def test_jvm_version(self):
        self.validate_path_regex(self.jvm_api, "getJavaVersion", r"^1\.[78]\.\d+_?\d*$")

    def test_gc_info(self):
        gc = self.curl(self.path_by_nick(self.jvm_api, "getGCinfo"))
        self.assertGreaterEqual(len(gc), 2)
        self.assert_key_in("count", gc[0])
        self.assert_key_in("name", gc[0])
        self.assert_key_in("time", gc[0])

    def test_force_gc(self):
        gc = self.curl(self.path_by_nick(self.jvm_api, "getGCinfo"))
        time.sleep(1)
        self.curl(self.path_by_nick(self.jvm_api, "forceGC"), True)
        gc1 = self.curl(self.path_by_nick(self.jvm_api, "getGCinfo"))
        self.assertGreaterEqual(len(gc), 2)
        self.assertGreater(gc1[0]["count"], gc[0]["count"])
        self.assertGreater(gc1[0]["time"], gc[0]["time"])

    def test_get_mbeans(self):
        gc = self.curl(self.path_by_nick(self.jvm_api, "getMbeanList"))
        self.assertGreaterEqual(len(gc), 10)
        self.assertGreaterEqual(gc.index("java.lang:type=Memory"), 0,
                                "Memory mbean is missing")

    def test_get_mbean(self):
        mbean = self.curl(self.path_by_nick(self.jvm_api, "getMbeanList") + 
                       urllib.quote("java.lang:name=PS Old Gen,type=MemoryPool"))        
        self.assertGreaterEqual(len(mbean), 15)
        self.assert_key_in("type", mbean[0])
        self.assert_key_in("name", mbean[0])
        self.assert_key_in("value", mbean[0])

    def test_set_mbean(self):
        path = self.path_by_nick(self.jvm_api, "getMbeanList") + urllib.quote("java.lang:name=PS Old Gen,type=MemoryPool")
        mbean = self.curl(path)
        usage = next((item for item in mbean if item["name"] == "UsageThreshold"), None)        
        self.assertTrue(usage != None)
        self.curl(path + "/UsageThreshold?value=" + str(usage["value"] + 1), True)
        mbean1 = self.curl(path)
        usage1 = next((item for item in mbean1 if item["name"] == "UsageThreshold"), None)
        self.assertEqual(usage["value"] + 1, usage1["value"])

    def test_list_file_cmd(self):
        path = "/file"
        lst = self.curl(path + "/etc?op=LISTSTATUS")
        hosts = next((item for item in lst if item["pathSuffix"] == "hosts"), None)
        self.assertEqual(hosts["owner"], "osv")

    def test_file_status_cmd(self):
        path = "/file"
        hosts = self.curl(path + "/etc/hosts?op=GETFILESTATUS")
        self.assertEqual(hosts["type"], "FILE")

    def test_put_file_cmd(self):
        path = "/file"
        self.curl_command(path + "/etc/hosts?op=COPY&destination="+urllib.quote("/etc/hosts1"), 'PUT')
        hosts = self.curl(path + "/etc/hosts1?op=GETFILESTATUS")
        self.assertEqual(hosts["type"], "FILE")
        self.curl_command(path + "/etc/hosts1?op=RENAME&destination="+urllib.quote("/etc/hosts2"), 'PUT')
        hosts = self.curl(path + "/etc/hosts2?op=GETFILESTATUS")
        self.assertEqual(hosts["type"], "FILE")
        hosts = self.curl(path + "/etc/hosts1?op=GETFILESTATUS")
        self.assertEqual(hosts, '')
        self.curl_command(path + "/etc/hosts2?op=DELETE", 'DELETE')
        hosts = self.curl(path + "/etc/hosts2?op=GETFILESTATUS")
        self.assertEqual(hosts, '')

    def make_temp_file(self):
        f = open('temp-test-file.txt', 'w')
        for x in range(0, 128000):
            f.write(str(x))
            f.write("\n")
        f.close()

    def test_file_upload(self):
        self.make_temp_file()
        path = "/file"
        target = path + "/usr/mgmt/test-file.txt"
        cmd = "curl -F filedata=@temp-test-file.txt " + self.get_url(target)
        os.system(cmd)
        hosts = self.curl(target + "?op=GETFILESTATUS")
        self.assertEqual(hosts["type"], "FILE")
        os.remove('temp-test-file.txt')
        cmd = "wget -O tmp-test-dwnld.txt " + self.get_url(target) + "?op=GET "
        os.system(cmd)
        count = 0
        with open('tmp-test-dwnld.txt', 'r') as f:
            for read_data in f:
                self.assertEqual(str(count)+'\n', read_data)
                count = count + 1
        self.assertEqual(count,128000)
        f.closed
        os.remove('tmp-test-dwnld.txt')

    def test_env(self):
        param = "test-param"
        get_path = self.path_by_nick(self.env_api, "getEnv") + param 
        set_path =get_path + "?val=TEST"
        self.curl(set_path, True)        
        val = self.curl(get_path)
        self.assertEqual(val, "TEST")

    @classmethod
    def curl(cls, api, post=False):
        url = cls.get_url(api)
        if post:
            data = urllib.urlencode({'Fake' : 'data-to-become-post'})
            req = urllib2.Request(url, data)
            response = urllib2.urlopen(req)
            return ""
        else:
            try:
                response = urllib2.urlopen(url)
            except:
                return ""
        return json.load(response)

    @classmethod
    def curl_command(cls, api, command):
        url = cls.get_url(api)
        opener = urllib2.build_opener(urllib2.HTTPHandler)
        request = urllib2.Request(url, data='')
        request.get_method = lambda: command
        opener.open(request)

    @classmethod
    def exec_os(cls):
        if config.use_sudo:
            return subprocess.Popen(["/usr/bin/sudo", config.run_script, "-n", "-e", config.cmd])
        return subprocess.Popen([config.run_script, "--forward", "tcp:" + str(config.port) + "::" + str(config.port), "-e", config.cmd])

    @classmethod
    def shutdown(cls):
        path = cls.path_by_nick(cls.os_api, "shutdown")
        try:
            cls.curl(path, True)
        except:
            pass
        retry = 10

        while cls.os_process.poll() == None:
            retry -= 1
            if retry == 0:
                raise Exception("Fail to shutdown server")
            time.sleep(1)

    @classmethod
    def setUpClass(cls):
        if not config.connect:
            cls.os_process = cls.exec_os()
        cls.os_api = cls.get_json_api("os.json")
        cls.jvm_api = cls.get_json_api("jvm.json")
        cls.file_api = cls.get_json_api("file.json")
        cls.env_api = cls.get_json_api("env.json")
        retry = 10
        while not cls.is_reachable():
            time.sleep(1)
            retry -= 1
            if retry == 0:
                cls.shutdown()
                raise Exception("Server is down")

    @classmethod
    def tearDownClass(cls):
        cls.shutdown()

if __name__ == '__main__':
    del sys.argv[1:]
    unittest.main()
