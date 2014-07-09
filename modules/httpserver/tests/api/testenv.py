#!/usr/bin/env python
import basetest

class testenv(basetest.Basetest):
    def test_env(self):
        param = "test-param"
        get_path = self.path_by_nick(self.env_api, "getEnv") + param
        set_path = get_path + "?val=TEST"
        self.curl(set_path, True)
        val = self.curl(get_path)
        self.assertEqual(val, "TEST")

    @classmethod
    def setUpClass(cls):
        cls.env_api = cls.get_json_api("env.json")
