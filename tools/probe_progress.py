#!/usr/bin/env python3
"""Polls Book Orbit reading progress of one book and prints every change.

Usage (from the firmware folder):  python3 tools/probe_progress.py [bookId]
Credentials and host are read from include/secrets.h. Ctrl+C to stop.
"""

import json
import os
import re
import sys
import time
import urllib.request

cfg = open("include/secrets.h" if os.path.exists("include/secrets.h") else "include/config.h", encoding="utf-8").read()


def define(name):
    m = re.search(r'#define\s+' + name + r'\s+"([^"]*)"', cfg)
    return m.group(1) if m else ""


host = define("DEFAULT_BOOKORBIT_HOST")
if not host.startswith("http"):
    host = "http://" + host
user, pw = define("DEFAULT_BOOKORBIT_USER"), define("DEFAULT_BOOKORBIT_PASS")
book = sys.argv[1] if len(sys.argv) > 1 else "1"


def req(method, path, body=None, token=None):
    r = urllib.request.Request(host + path, method=method, data=json.dumps(body).encode() if body is not None else None)
    r.add_header("Content-Type", "application/json")
    if token:
        r.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(r, timeout=10) as resp:
        return resp.status, dict(resp.headers), resp.read().decode()


st, _, body = req("POST", "/api/v1/auth/login", {"username": user, "password": pw})
j = json.loads(body)
token = j.get("accessToken") or j.get("access_token") or j.get("token") or (j.get("data") or {}).get("accessToken")
if not token:
    sys.exit("login: no token in response keys %s" % list(j.keys()))
print("logged in, polling book %s every 3 s; change the progress in the web reader now" % book)

last = None
while True:
    st, hdr, body = req("GET", "/api/v1/books/%s/progress" % book, token=token)
    if body != last:
        cache = {k: v for k, v in hdr.items() if k.lower() in ("cache-control", "etag", "last-modified", "age", "date")}
        print("\n=== %s HTTP %d %s" % (time.strftime("%H:%M:%S"), st, cache))
        try:
            print(json.dumps(json.loads(body), ensure_ascii=False, indent=1))
        except Exception:
            print(body)
        last = body
    else:
        print(".", end="", flush=True)
    time.sleep(3)
