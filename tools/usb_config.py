#!/usr/bin/env python3
"""Serve the offline USB configuration page on loopback, using only stdlib."""

from __future__ import annotations

import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
import webbrowser

PAGE_ROOT = Path(__file__).resolve().parent / "usb-config"
ASSETS = {"/": "index.html", "/index.html": "index.html", "/style.css": "style.css",
          "/app.mjs": "app.mjs", "/protocol.mjs": "protocol.mjs", "/models.mjs": "models.mjs"}


class ConfigPageHandler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".mjs": "text/javascript"}

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(PAGE_ROOT), **kwargs)

    def do_GET(self):
        port = self.server.server_address[1]
        # Serve only the configuration assets, never the checkout or arbitrary
        # Host names. There is no HTTP endpoint that can write device settings.
        if self.headers.get("Host") not in {f"127.0.0.1:{port}", f"localhost:{port}"}:
            self.send_error(403)
            return
        path = urlsplit(self.path).path
        if path not in ASSETS:
            self.send_error(404)
            return
        self.path = "/" + ASSETS[path]
        super().do_GET()

    def do_HEAD(self):
        self.do_GET()

    def copyfile(self, source, outputfile):
        if self.command != "HEAD":
            super().copyfile(source, outputfile)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Permissions-Policy", "serial=(self)")
        self.send_header("Content-Security-Policy",
                         "default-src 'none'; script-src 'self'; style-src 'self'; "
                         "img-src 'self'; connect-src 'none'; base-uri 'none'; "
                         "frame-ancestors 'none'; form-action 'none'")
        super().end_headers()

    def log_message(self, format, *args):
        pass


def create_server(port=8765):
    return ThreadingHTTPServer(("127.0.0.1", port), ConfigPageHandler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--open", action="store_true", help="Open the page in the default browser")
    args = parser.parse_args()
    try:
        server = create_server(args.port)
    except OSError as error:
        parser.exit(1, f"Cannot start the local configuration page: {error}\n")
    with server:
        url = f"http://127.0.0.1:{server.server_address[1]}/"
        print(f"AI Passport USB configuration: {url}", flush=True)
        print("Use desktop Chrome or Edge. USB access starts only after you select a device.", flush=True)
        print("Press Ctrl+C to stop.", flush=True)
        if args.open:
            webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
