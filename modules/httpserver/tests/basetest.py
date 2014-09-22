#!/usr/bin/env python
import json
import os
import subprocess
import time
import unittest
import socket
import requests

from osv import client

class HttpError(BaseException):
    def __init__(self, code):
        self.code = code

class Basetest(unittest.TestCase):
    @classmethod
    def set_config(cls, parser):
        cls.config = parser.parse_args()
        cls._client = client.Client(cls.config)

    @classmethod
    def get_url(cls, uri):
        return cls._client.get_url() + uri

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
            return bool(cls.curl(cls.path_by_nick(cls.jvm_api, "getJavaVersion")))
        except HttpError:
            return False

    @classmethod
    def is_reachable(cls):
        s = socket.socket()
        try:
            s.connect((cls._client.get_host(), cls._client.get_port()))
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
        except HttpError as e:
            if e.code != code:
                raise Exception('Expected error code %d but got %d' % (code, e.code))
        else:
            raise Exception('Expected failure but request succeeded')

    @classmethod
    def curl(cls, api, method='GET', data=None):
        url = cls.get_url(api)

        r = {
            'GET': requests.get,
            'POST': requests.post,
            'DELETE': requests.delete,
            'PUT': requests.put,
        }[method](url, data=data, **cls._client.get_request_kwargs())

        if r.status_code != 200:
            raise HttpError(r.status_code)

        if r.text:
            return r.json()

    @classmethod
    def get_client_cert_path(cls):
        return cls._client.get_client_cert_path()

    @classmethod
    def get_client_key_path(cls):
        return cls._client.get_client_key_path()

    @classmethod
    def get_ca_cert_path(cls):
        return cls._client.get_cacert_path()

    @classmethod
    def exec_os(cls):
        args = []
        if cls.config.use_sudo:
            args += ["/usr/bin/sudo", cls.config.run_script, "-n"]
        else:
            args += [cls.config.run_script, "--forward", "tcp:" + str(cls._client.get_port()) + "::" + str(cls._client.get_port())]

        if cls.config.cmd:
            args += ["-e", cls.config.cmd]

        return subprocess.Popen(args)

    @classmethod
    def shutdown(cls):
        if cls.config.connect:
            return

        path = cls.path_by_nick(cls.os_api, "os_shutdown")
        try:
            cls.curl(path, method='POST')
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
