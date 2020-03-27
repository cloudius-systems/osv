#!/usr/bin/env python3
import basetest

class testenv(basetest.Basetest):
    def test_env(self):
        param = "test-param"
        get_path = self.path_by_nick(self.env_api, "list_env") + param
        set_path = get_path + "?val=TEST"
        self.assertHttpError(set_path, 404, method='POST')

        lst = self.curl(self.path_by_nick(self.env_api, "list_env"))
        if not "OSV_CPUS=4" in lst:
            raise Exception('environment variable OSV_CPUS not found in list')

        self.assertHttpError(get_path, 404, method="DELETE")

    @classmethod
    def setUpClass(cls):
        cls.env_api = cls.get_json_api("env.json")
