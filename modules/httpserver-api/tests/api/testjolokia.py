#!/usr/bin/env python
import basetest
import json

class testjolokia(basetest.Basetest):
    def setUp(self):
        self.path = '/jolokia'

    def check_response(self, res):
        self.assert_key_in('timestamp', res)
        self.assert_key_in('status', res)

    def test_version(self):
        res = self.curl(self.path)
        self.check_response(res)
        self.assert_key_in('request', res)
        self.assertEqual('version', res['request']['type'])
        self.assertEqual('OSv Jolokia Bridge', res['value']['config']['agentId'])

    def test_get_read_simple(self):
        res = self.curl(self.path + '/read/java.lang:type=Memory/HeapMemoryUsage')
        self.check_response(res)
        value = res['value']
        for k in ['max', 'committed', 'init', 'used']:
            self.assert_key_in(k, value)

    def test_post_read_simple(self):
        body = {
            'type' : 'read',
            'mbean' : 'java.lang:type=Memory',
            'attribute' : 'HeapMemoryUsage',
          }
        res = self.curl(self.path, method='POST', data=json.dumps(body))
        self.check_response(res)
        value = res['value']
        for k in ['max', 'committed', 'init', 'used']:
            self.assert_key_in(k, value)

    def test_get_write_simple(self):
        res = self.curl(self.path + '/write/java.lang:type=Memory/Verbose/true')
        try:
            self.check_response(res)
            res = self.curl(self.path + '/read/java.lang:type=Memory/Verbose')
            self.assertTrue(res['value'])
        finally:
            res = self.curl(self.path + '/write/java.lang:type=Memory/Verbose/false')

    def test_post_write_simple(self):
        body = {
            'type' : 'write',
            'mbean' : 'java.lang:type=Memory',
            'attribute' : 'Verbose',
            'value' : 'true'
          }
        res = self.curl(self.path, method='POST', data=json.dumps(body))
        try:
            self.check_response(res)
            res = self.curl(self.path + '/read/java.lang:type=Memory/Verbose')
            self.assertTrue(res['value'])
        finally:
            res = self.curl(self.path + '/write/java.lang:type=Memory/Verbose/false')


#        {"timestamp":1410785481,"status":200,"request":{"type":"version"},"value":{"protocol":"7.2","config":{"agentId":"OSv Jolokia Bridge"},"agent":"1.2.2","info":{}}}
