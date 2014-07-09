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

class Basetest(unittest.TestCase):
    @classmethod
    def set_config(cls, parser):
        cls.config = parser.parse_args()

    @classmethod
    def get_url(cls, uri):
        return 'http://' + cls.config.ip + ':' + str(cls.config.port) + uri

    @classmethod
    def get_json_api(cls, name):
        json_data = open(os.path.join(cls.config.jsondir, name))
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
        try:
            return cls.curl(cls.path_by_nick(cls.jvm_api, "getJavaVersion")) != ""
        except urllib2.HTTPError:
            return False

    @classmethod
    def is_reachable(cls):
        s = socket.socket()
        try:
            s.connect((cls.config.ip, cls.config.port))
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


    def assertHttpError(self, url, code=404):
        try:
            self.curl(url)
            raise Exception('The request for %s should have failed!' % url)
        except urllib2.HTTPError as e:
            if e.code != code:
                raise Exception('Expected error code %d but got %d' % (code, e.code))


    @classmethod
    def curl(cls, api, post=False):
        url = cls.get_url(api)
        if post:
            data = urllib.urlencode({'Fake' : 'data-to-become-post'})
            req = urllib2.Request(url, data)
            response = urllib2.urlopen(req)
        else:
            response = urllib2.urlopen(url)

        response_text = ''.join(response.readlines())

        if response_text:
            return json.loads(response_text)

    @classmethod
    def curl_command(cls, api, command):
        url = cls.get_url(api)
        opener = urllib2.build_opener(urllib2.HTTPHandler)
        request = urllib2.Request(url, data='')
        request.get_method = lambda: command
        opener.open(request)

    @classmethod
    def exec_os(cls):
        args = []
        if cls.config.use_sudo:
            args += ["/usr/bin/sudo", cls.config.run_script, "-n"]
        else:
            args += [cls.config.run_script, "--forward", "tcp:" + str(cls.config.port) + "::" + str(cls.config.port)]

        if cls.config.cmd:
            args += ["-e", cls.config.cmd]

        return subprocess.Popen(args)

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
    def start_image(cls):
        cls.jvm_api = cls.get_json_api("jvm.json")
        cls.os_api = cls.get_json_api("os.json")
        if not cls.config.connect:
            cls.os_process = cls.exec_os()
        retry = 10
        while not cls.is_reachable():
            time.sleep(1)
            retry -= 1
            if retry == 0:
                cls.shutdown()
                raise Exception("Server is down")
