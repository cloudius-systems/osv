#!/usr/bin/env python
import time
import basetest

class testnetwork(basetest.Basetest):
    def test_ifconfig(self):
        path = self.path_by_nick(self.network_api, "listIfconfig")
        intf = self.curl(path + 'lo0')
        self.assert_key_in("data", intf)
        self.assert_key_in("config", intf)
        conf = intf["config"]
        self.assert_key_in("phys_addr", conf)
        self.assert_key_in("mask", conf)
        self.assertEqual(conf["addr"], "127.0.0.1")
        lst = self.curl(path)
        self.assertNotEqual(lst, [])

    def test_get_routes(self):
        path = self.path_by_nick(self.network_api, "getRoute")
        routes = self.curl(path)
        route = routes[0]
        self.assert_key_in("destination", route)
        self.assert_key_in("gateway", route)
        self.assert_key_in("flags", route)
        self.assert_key_in("netif", route)
        self.assert_key_in("ipv6", route)

    @classmethod
    def setUpClass(cls):
        cls.network_api = cls.get_json_api("network.json")
