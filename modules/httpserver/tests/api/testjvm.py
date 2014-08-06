#!/usr/bin/env python
import time
import basetest
import urllib

class testjvm(basetest.Basetest):
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
        self.curl(self.path_by_nick(self.jvm_api, "forceGC"), method='POST')
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
        self.curl(path + "/UsageThreshold?value=" + str(usage["value"] + 1), method='POST')
        mbean1 = self.curl(path)
        usage1 = next((item for item in mbean1 if item["name"] == "UsageThreshold"), None)
        self.assertEqual(usage["value"] + 1, usage1["value"])
