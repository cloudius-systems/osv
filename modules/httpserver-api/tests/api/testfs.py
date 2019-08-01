#!/usr/bin/env python
import basetest

class testfs(basetest.Basetest):
    def test_getfs(self):
        get_path = "/fs/df/"
        ls = self.curl(get_path)
        val = ls[0]
        self.assertEqual(val["mount"], "/")
        self.assertGreaterEqual(val["ffree"], 200000)
        self.assertGreaterEqual(val["ftotal"], 200000)
        self.assertGreaterEqual(val["bfree"], 200000)
        self.assertGreaterEqual(val["btotal"], 200000)
        self.assertEqual(val["filesystem"], "/dev/vblk0.1")

    @classmethod
    def setUpClass(cls):
        cls.fs_api = cls.get_json_api("fs.json")
