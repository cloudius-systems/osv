#!/usr/bin/env python
import os
import urllib
import basetest

class testfile(basetest.Basetest):
    def test_list_file_cmd(self):
        path = "/file"
        lst = self.curl(path + "/etc?op=LISTSTATUS")
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
        self.curl_command(path + "/etc/hosts?op=COPY&destination=" + urllib.quote("/etc/hosts1"), 'PUT')
        hosts = self.curl(path + "/etc/hosts1?op=GETFILESTATUS")
        self.assertEqual(hosts["type"], "FILE")
        self.curl_command(path + "/etc/hosts1?op=RENAME&destination=" + urllib.quote("/etc/hosts2"), 'PUT')
        hosts = self.curl(path + "/etc/hosts2?op=GETFILESTATUS")
        self.assertEqual(hosts["type"], "FILE")
        self.assertHttpError(path + "/etc/hosts1?op=GETFILESTATUS")
        self.curl_command(path + "/etc/hosts2?op=DELETE", 'DELETE')
        self.assertHttpError(path + "/etc/hosts2?op=GETFILESTATUS")

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
                self.assertEqual(str(count) + '\n', read_data)
                count = count + 1
        self.assertEqual(count, 128000)
        f.closed
        os.remove('tmp-test-dwnld.txt')

    @classmethod
    def setUpClass(cls):
        cls.file_api = cls.get_json_api("file.json")
