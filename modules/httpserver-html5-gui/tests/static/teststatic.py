import requests
import basetest

class teststatic(basetest.Basetest):
    def testredirect(self):
        r = self.get("/")
        self.assertEqual(r.status_code, 301)
        self.assertEqual(r.url, self.get_url("/"))

    def testapiguifile(self):
        r = self.get("/api-gui/")
        self.assertEqual(r.status_code, 200)

    def testapi(self):
        r = self.get("/api/listings/api.json")
        self.assertEqual(r.status_code, 200)

    def testdashboard_static(self):
        r = self.get("/dashboard_static/smoothie.js")
        self.assertEqual(r.status_code, 200)

    def testdashboard(self):
        r = self.get("/dashboard/")
        self.assertEqual(r.status_code, 200)

    def testapigui(self):
        r = self.get("/api-gui/css/screen.css")
        self.assertEqual(r.status_code, 200)

    def get(self,path):
        return requests.get(self.get_url(path), allow_redirects=False)
