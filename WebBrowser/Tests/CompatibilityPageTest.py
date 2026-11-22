#!/usr/bin/env python3
"""Optional local Chromium validation of the diagnostic page, not Qt WebEngine."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread

from playwright.sync_api import sync_playwright


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header(
            "Content-Security-Policy",
            "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
            "media-src blob:; connect-src 'none'; base-uri 'none'; frame-ancestors 'none'",
        )
        super().end_headers()

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--chromium", required=True, help="Local Chromium executable")
    parser.add_argument("--test-only-no-sandbox", action="store_true",
                        help="For restricted test containers; only serves the local diagnostic fixture")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent.parent / "UI"
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=directory))
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with sync_playwright() as pw:
            browser = pw.chromium.launch(executable_path=args.chromium, headless=True,
                                         chromium_sandbox=not args.test_only_no_sandbox)
            try:
                page = browser.new_page()
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                page.goto(f"http://127.0.0.1:{server.server_port}/Compatibility.html")
                page.wait_for_function("document.title.startsWith('COMPAT-')", timeout=40000)
                print(page.title())
                assert page.title().startswith("COMPAT-PASS"), page.locator("#report").inner_text()
                page.locator("#call").click()
                page.wait_for_function(
                    "document.querySelector('#callStatus').textContent.startsWith('PASS:') || "
                    "document.querySelector('#callStatus').textContent.startsWith('FAIL:')",
                    timeout=35000,
                )
                status = page.locator("#callStatus").inner_text()
                print(status)
                assert status.startswith("PASS:"), status
                assert not errors, errors
            finally:
                browser.close()
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
