#!/usr/bin/env python3
"""Finds out how Book Orbit accepts a CFI in PUT /api/v1/reading-progress.

Usage (from the firmware folder):  python3 tools/probe_cfi_write.py [bookId]
Tries a few request shapes on the book, reads the progress back after each, and
finally restores the progress that was there before. Login from include/secrets.h.
"""
import json, re, sys, urllib.request, urllib.error

import os
cfg = open("include/secrets.h" if os.path.exists("include/secrets.h") else "include/config.h", encoding="utf-8").read()
def define(name):
    m = re.search(r'#define\s+' + name + r'\s+"([^"]*)"', cfg)
    return m.group(1) if m else ""
host = define("DEFAULT_BOOKORBIT_HOST")
if not host.startswith("http"): host = "http://" + host
book = sys.argv[1] if len(sys.argv) > 1 else "1"

def req(method, path, body=None, token=None):
    r = urllib.request.Request(host + path, method=method,
                               data=json.dumps(body).encode() if body is not None else None)
    r.add_header("Content-Type", "application/json")
    if token: r.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(r, timeout=10) as resp:
            return resp.status, resp.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()

st, body = req("POST", "/api/v1/auth/login", {"username": define("DEFAULT_BOOKORBIT_USER"),
                                               "password": define("DEFAULT_BOOKORBIT_PASS")})
j = json.loads(body)
token = j.get("accessToken") or j.get("token") or j.get("access_token")
def get():
    st, b = req("GET", "/api/v1/books/%s/progress" % book, token=token)
    items = json.loads(b)
    items = items if isinstance(items, list) else items.get("items", [])
    return items[0] if items else None

orig = get()
if not orig: sys.exit("book %s has no progress record; open it in the web reader first" % book)
print("before:", json.dumps({k: orig.get(k) for k in ("fileId", "percentage", "cfi", "pageNumber", "koreaderProgress", "updatedAt")}, ensure_ascii=False))
fid = orig["fileId"]
test_cfi = "epubcfi(/6/8!/4/2/1:0)"
variants = [
    ("text.cfi only", {"fileId": fid, "percentage": 1.5, "text": {"cfi": test_cfi}}),
    ("text.cfi + koreader", {"fileId": fid, "percentage": 1.6, "text": {"cfi": test_cfi}, "koreader": "/body/DocFragment[4]/body/p[1]/text().0"}),
    ("koreader only (cfi kept?)", {"fileId": fid, "percentage": 1.7, "koreader": "/body/DocFragment[3]/body/p[1]/text().0"}),
]
for name, w in variants:
    st, b = req("PUT", "/api/v1/reading-progress", {"writes": [w]}, token)
    now = get()
    print("\n%-48s PUT -> %d %s" % (name, st, b[:300]))
    print("   after: percentage=%s cfi=%s pageNumber=%s koreaderProgress=%s" % (
        now.get("percentage"), now.get("cfi"), now.get("pageNumber"), now.get("koreaderProgress")))

restore = {"fileId": fid, "percentage": orig.get("percentage")}
text = {}
if orig.get("cfi"): text["cfi"] = orig["cfi"]
if orig.get("pageNumber"): text["page"] = orig["pageNumber"]
if text: restore["text"] = text
if orig.get("koreaderProgress"): restore["koreader"] = orig["koreaderProgress"]
st, b = req("PUT", "/api/v1/reading-progress", {"writes": [restore]}, token)
now = get()
print("\nrestored -> %d: percentage=%s cfi=%s" % (st, now.get("percentage"), now.get("cfi")))
