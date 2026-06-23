#!/usr/bin/env python3
# DESY Visual Events Server
# Polls mevacoind for new blocks on Dec 18, computes visual rank,
# and serves top-5 rankings via HTTP.

import argparse
import json
import os
import sys
import time
import requests
from requests.auth import HTTPDigestAuth
from http.server import HTTPServer, BaseHTTPRequestHandler
from datetime import datetime, timezone
from threading import Thread

DESY_MEMORIAL_MONTH = 12
DESY_MEMORIAL_DAY = 18

def desy_visual_rank(block_hash_hex: str) -> int:
    h = int(block_hash_hex[:16], 16)
    return h % 100

def is_desy_today() -> bool:
    now = datetime.now(timezone.utc)
    return now.month == DESY_MEMORIAL_MONTH and now.day == DESY_MEMORIAL_DAY

def make_rpc(session, url: str, method: str, params=None):
    payload = {"jsonrpc": "2.0", "id": "0", "method": method, "params": params or {}}
    try:
        resp = session.post(url, json=payload, timeout=10)
        resp.raise_for_status()
        return resp.json()
    except Exception as e:
        return {"error": str(e)}

class EventStore:
    def __init__(self, path: str):
        self.path = path
        self.date = None
        self.events = []
        self.by_height = {}
        self.total_blocks = 0
        self._load()

    def _load(self):
        if os.path.exists(self.path):
            try:
                with open(self.path) as f:
                    data = json.load(f)
                self.date = data.get("date")
                self.events = data.get("events", [])
                self.total_blocks = data.get("total_blocks", 0)
                self.by_height = {e["height"]: e for e in self.events}
            except Exception:
                self.events = []
                self.by_height = {}

    def _save(self):
        os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
        with open(self.path, "w") as f:
            json.dump({
                "date": self.date,
                "events": self.events,
                "total_blocks": self.total_blocks,
            }, f, indent=2)

    def reset_if_new_day(self):
        today = datetime.now(timezone.utc).strftime("%Y-%m-%d")
        if self.date != today:
            self.date = today
            self.events = []
            self.by_height = {}
            self.total_blocks = 0
            self._save()

    def add_block(self, height: int, block_hash: str, timestamp: int):
        if not is_desy_today():
            return
        if height in self.by_height:
            return
        rank = desy_visual_rank(block_hash)
        entry = {"height": height, "rank": rank, "hash": block_hash, "timestamp": timestamp}
        self.events.append(entry)
        self.by_height[height] = entry
        self.total_blocks += 1
        self.events.sort(key=lambda e: e["rank"], reverse=True)
        self.events = self.events[:5]
        self._save()

    def get_top5(self) -> list:
        return self.events

    def check(self, height: int) -> dict:
        entry = self.by_height.get(height)
        if not entry:
            return {"height": height, "is_visual_event": False}
        rank = entry["rank"]
        position = next((i+1 for i, e in enumerate(self.events) if e["height"] == height), None)
        return {
            "height": height,
            "hash": entry["hash"],
            "rank": rank,
            "is_visual_event": position is not None,
            "position": position,
        }

class EventHTTPHandler(BaseHTTPRequestHandler):
    store = None

    def do_GET(self):
        parsed = self.path.split("?")
        path = parsed[0]
        qs = {}
        if len(parsed) > 1:
            for pair in parsed[1].split("&"):
                if "=" in pair:
                    k, v = pair.split("=", 1)
                    qs[k] = v

        if path == "/events/top5":
            data = {
                "date": EventHTTPHandler.store.date,
                "events": EventHTTPHandler.store.get_top5(),
                "total_blocks": EventHTTPHandler.store.total_blocks,
                "updated_at": datetime.now(timezone.utc).isoformat(),
            }
            self._json(200, data)
        elif path == "/events/check":
            height = qs.get("height")
            if height is None or not height.isdigit():
                self._json(400, {"error": "missing or invalid height"})
                return
            self._json(200, EventHTTPHandler.store.check(int(height)))
        elif path == "/health":
            self._json(200, {"status": "ok", "is_desy_today": is_desy_today()})
        else:
            self._json(404, {"error": "not found"})

    def _json(self, status: int, data: dict):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())

    def log_message(self, fmt, *args):
        sys.stderr.write("[%s] %s\n" % (self.log_date_time_string(), fmt % args))

def main():
    parser = argparse.ArgumentParser(description="DESY Visual Events Server")
    parser.add_argument("--rpc-url", default="http://127.0.0.1:18081/json_rpc",
                        help="mevacoind RPC URL")
    parser.add_argument("--rpc-login", default="",
                        help="RPC auth user:password")
    parser.add_argument("--http-port", type=int, default=8080,
                        help="HTTP server port")
    parser.add_argument("--data-dir", default=os.path.expanduser("~/.mevacoin/events"),
                        help="Data directory for persistence")
    parser.add_argument("--poll-interval", type=int, default=10,
                        help="Poll interval in seconds")
    args = parser.parse_args()

    store_path = os.path.join(args.data_dir, "visual_events.json")
    store = EventStore(store_path)
    EventHTTPHandler.store = store

    session = requests.Session()
    if args.rpc_login:
        user, _, pw = args.rpc_login.partition(":")
        session.auth = HTTPDigestAuth(user, pw)

    last_height = 0
    try:
        resp = make_rpc(session, args.rpc_url, "get_info")
        if "result" in resp:
            last_height = resp["result"].get("height", 0) - 1
    except Exception:
        pass

    httpd = HTTPServer(("0.0.0.0", args.http_port), EventHTTPHandler)
    Thread(target=httpd.serve_forever, daemon=True).start()
    print(f"[DESY] Server HTTP su http://0.0.0.0:{args.http_port}")
    print(f"[DESY] URL RPC: {args.rpc_url}")
    print(f"[DESY] Dati in: {args.data_dir}")
    print(f"[DESY] Polling ogni {args.poll_interval}s (ultima altezza={last_height})")

    while True:
        try:
            store.reset_if_new_day()
            resp = make_rpc(session, args.rpc_url, "get_info")
            if "error" in resp:
                print(f"[DESY] Errore RPC: {resp['error']}", file=sys.stderr)
                time.sleep(args.poll_interval)
                continue
            current_height = resp["result"]["height"] - 1
            for h in range(last_height + 1, current_height + 1):
                block_resp = make_rpc(session, args.rpc_url,
                                      "get_block_header_by_height", {"height": h})
                if "result" in block_resp:
                    bh = block_resp["result"]["block_header"]
                    store.add_block(h, bh["hash"], bh["timestamp"])
                    if h % 10 == 0:
                        print(f"[DESY] Processato blocco {h} (rank={desy_visual_rank(bh['hash'])})")
            last_height = current_height
        except KeyboardInterrupt:
            print("\n[DESY] Arresto")
            break
        except Exception as e:
            print(f"[DESY] Errore: {e}", file=sys.stderr)
        time.sleep(args.poll_interval)

if __name__ == "__main__":
    main()
