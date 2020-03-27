#!/usr/bin/env python3
import os
import urllib.request, urllib.parse, urllib.error
import basetest
import subprocess

class testfile(basetest.Basetest):
    def build_curl_cmd(self, args):
        if not self._client.is_ssl():
            return 'curl ' + args
        return 'curl --cacert %s --cert %s --key %s %s' % (
            self.get_ca_cert_path(), self.get_client_cert_path(), self.get_client_key_path(), args)

    def build_wget_cmd(self, args):
        if not self._client.is_ssl():
            return 'wget ' + args
        return 'wget --ca-certificate=%s --certificate=%s --private-key=%s %s' % (
            self.get_ca_cert_path(), self.get_client_cert_path(), self.get_client_key_path(), args)

    def test_list_file_cmd(self):
        path = "/file"
        lst = self.curl(path + "/etc?op=LISTSTATUS")
        hosts = next((item for item in lst if item["pathSuffix"] == "hosts"), None)
        self.assertEqual(hosts["owner"], "osv")

    def test_list_astrik_file_cmd(self):
        path = "/file"
        lst = self.curl(path + "/et%3F/hos*?op=LISTSTATUS")
        hosts = next((item for item in lst if item["pathSuffix"] == "hosts"), None)
        self.assertEqual(hosts["owner"], "osv")

    def test_file_status_cmd(self):
        path = "/file"
        hosts = self.curl(path + "/etc/hosts?op=GETFILESTATUS")
        self.assertEqual(hosts["type"], "FILE")
        self.assert_between("accessTime", 1300000000, 2000000000, hosts["accessTime"])
        self.assertEqual(hosts["blockSize"], 512)
        self.assertEqual(hosts["group"], "osv")
        self.assert_between("length", 20, 40, hosts["length"])
        self.assert_between("modificationTime", 1300000000, 2000000000, hosts["modificationTime"])
        self.assertEqual(hosts["owner"], "osv")
        self.assertEqual(hosts["pathSuffix"], "hosts")
        self.assertEqual(hosts["permission"], "664")
        self.assertEqual(hosts["replication"], 1)

    def test_put_file_cmd(self):
        path = "/file"
        self.assertHttpError(path + "/etc/hosts?op=COPY&destination="+urllib.parse.quote("/etc/hosts1"), 404, method='PUT')
        self.assertHttpError(path + "/etc/hosts1?op=RENAME&destination="+urllib.parse.quote("/etc/hosts2"), 404, method='PUT')
        self.assertHttpError(path + "/etc/hosts2?op=DELETE", 404, method='DELETE')

    @classmethod
    def setUpClass(cls):
        cls.file_api = cls.get_json_api("file.json")
