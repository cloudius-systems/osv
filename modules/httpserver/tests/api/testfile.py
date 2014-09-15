#!/usr/bin/env python
import os
import urllib
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
        self.assertEqual(hosts["permission"], "666")
        self.assertEqual(hosts["replication"], 1)

    def test_put_file_cmd(self):
        path = "/file"
        self.curl(path + "/etc/hosts?op=COPY&destination="+urllib.quote("/etc/hosts1"), method='PUT')
        hosts = self.curl(path + "/etc/hosts1?op=GETFILESTATUS")
        self.assertEqual(hosts["type"], "FILE")
        self.curl(path + "/etc/hosts1?op=RENAME&destination="+urllib.quote("/etc/hosts2"), method='PUT')
        hosts = self.curl(path + "/etc/hosts2?op=GETFILESTATUS")
        self.assertEqual(hosts["type"], "FILE")
        self.assertHttpError(path + "/etc/hosts1?op=GETFILESTATUS")
        self.curl(path + "/etc/hosts2?op=DELETE", method='DELETE')
        self.assertHttpError(path + "/etc/hosts2?op=GETFILESTATUS")

    def make_temp_file(self):
        f = open('temp-test-file.txt', 'w')
        for x in range(0, 128000):
            f.write(str(x))
            f.write("\n")
        f.close()

    def test_file_upload(self):
        self.make_temp_file()
        target = "/file/usr/mgmt/test-file.txt"
        subprocess.check_call(self.build_curl_cmd("-F filedata=@temp-test-file.txt " + self.get_url(target)).split())
        hosts = self.curl(target + "?op=GETFILESTATUS")
        self.assertEqual(hosts["type"], "FILE")
        os.remove('temp-test-file.txt')
        subprocess.check_call(self.build_wget_cmd("-O tmp-test-dwnld.txt " + self.get_url(target) + "?op=GET ").split())
        count = 0
        with open('tmp-test-dwnld.txt', 'r') as f:
            for read_data in f:
                self.assertEqual(str(count) + '\n', read_data)
                count = count + 1
        self.assertEqual(count, 128000)
        f.closed
        os.remove('tmp-test-dwnld.txt')

    @classmethod
    def setUpClass(cls):
        cls.file_api = cls.get_json_api("file.json")
