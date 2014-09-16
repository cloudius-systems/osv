#!/usr/bin/env python
import time
import basetest

class testtrace(basetest.Basetest):
    def setUp(self):
        self.path = "/trace"
    def tearDown(self):
        self.curl(self.path + '/status?enabled=false&backtrace=false', method='POST')

    def check_status_list(self, list):
        for s in list:
            self.assert_key_in("backtrace", s)
            self.assert_key_in("enabled", s)
            self.assert_key_in("name", s)
            self.assert_key_in("id", s)


    def test_get_status(self):
        status = self.curl(self.path + "/status")
        self.assertGreaterEqual(status, 0)
        self.check_status_list(status)

    def test_set_status(self):
        for bt in [True, False]:
            for en in [True, False]:
                status = self.curl(self.path + '/status?enabled=' + str(en) + '&backtrace=' + str(bt), method='POST')
                self.assertGreaterEqual(status, 0)
                self.check_status_list(status)
                status = self.curl(self.path + "/status")
                for s in status:
                    self.assertEqual(bt, s['backtrace'])
                    self.assertEqual(en, s['enabled'])

    def test_get_single_eventinfo(self):
        status = self.curl(self.path + "/status")
        for s in status:
            s2 = self.curl(self.path + '/event/' + s['id'])
            self.assertEqual(s2, s)

    def test_set_single_eventinfo(self):
        status = self.curl(self.path + "/status")
        for s in status:
            for bt in [True, False]:
                for en in [True, False]:
                    s2 = self.curl(self.path + '/event/' + s['id'] + '?enabled=' + str(en) + '&backtrace=' + str(bt), method='POST')
                    s3 = self.curl(self.path + '/event/' + s['id'])
                    self.assertEqual(bt, s3['backtrace'])
                    self.assertEqual(en, s3['enabled'])

    def test_get_trace_dump(self):
        self.curl(self.path + '/status?enabled=true&backtrace=true', method='POST')
        try:
            self.curl(self.path + '/buffers')
        except ValueError: # not json. that is fine
            pass
