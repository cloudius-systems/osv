#!/usr/bin/env python
import basetest

class testenv(basetest.Basetest):
    def test_env(self):
        param = "test-param"
        get_path = self.path_by_nick(self.env_api, "getEnv") + param
        set_path = get_path + "?val=TEST"
        self.curl(set_path, method='POST')
        val = self.curl(get_path)
        self.assertEqual(val, "TEST")

        self.curl(get_path, method="DELETE")
        self.assertHttpError(get_path, 400)

    @classmethod
    def setUpClass(cls):
        cls.env_api = cls.get_json_api("env.json")
