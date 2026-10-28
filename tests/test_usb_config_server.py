#!/usr/bin/env python3
"""The USB configuration server exposes only its bundled, loopback-only UI."""

import http.client
import importlib.util
from pathlib import Path
import threading
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("usb_config_server", ROOT / "tools/usb_config.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = module.create_server(0)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.port = cls.server.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def request(self, method, path, **headers):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=3)
        try:
            connection.request(method, path, headers=headers)
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def test_offline_assets_and_browser_policy(self):
        for path in module.ASSETS:
            status, headers, body = self.request("GET", path)
            self.assertEqual(status, 200)
            self.assertTrue(body)
            self.assertEqual(headers["Permissions-Policy"], "serial=(self)")
            self.assertIn("connect-src 'none'", headers["Content-Security-Policy"])
        _, headers, _ = self.request("GET", "/app.mjs")
        self.assertIn("javascript", headers["Content-type"])

    def test_checkout_and_traversal_are_inaccessible(self):
        for path in ["/main/main.c", "/../AGENTS.md", "/%2e%2e/AGENTS.md", "/.git/config"]:
            self.assertEqual(self.request("GET", path)[0], 404)

    def test_unrelated_host_is_rejected(self):
        self.assertEqual(self.request("GET", "/", Host="unrelated.example")[0], 403)

    def test_hosted_headers_match_local_policy(self):
        # Cloudflare Pages serves the same files with tools/usb-config/_headers.
        # The hosted page must keep the local security policy, notably
        # connect-src 'none': the page talks only to the USB device.
        hosted = {}
        for line in (module.PAGE_ROOT / "_headers").read_text().splitlines()[1:]:
            name, value = line.strip().split(": ", 1)
            hosted[name.lower()] = value
        _, headers, _ = self.request("GET", "/")
        local = {name.lower(): value for name, value in headers.items()}
        for name in ["x-content-type-options", "referrer-policy", "permissions-policy",
                     "content-security-policy"]:
            self.assertEqual(hosted[name], local[name], name)
        self.assertNotIn("/_headers", module.ASSETS)

    def test_head_has_no_body(self):
        status, _, body = self.request("HEAD", "/")
        self.assertEqual(status, 200)
        self.assertEqual(body, b"")


if __name__ == "__main__":
    unittest.main()
