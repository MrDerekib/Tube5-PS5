#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Submit a YouTube URL to the local Tube5 preparation service."""
import argparse
from http.client import HTTPConnection
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", nargs="?", help="Omit to query preparation status")
    parser.add_argument("--height", type=int, choices=(360, 720, 1080), default=360)
    parser.add_argument("--seconds", type=int, choices=(60, 300, 600), default=60,
                        help="HD extract length; 360p prepares the full video (maximum one hour)")
    parser.add_argument("--port", type=int, default=8081)
    args = parser.parse_args()
    try:
        token = (Path(__file__).resolve().parents[1] / ".local/bridge/control-token").read_text().strip()
        connection = HTTPConnection("127.0.0.1", args.port, timeout=5)
        payload = json.dumps(dict(url=args.url, height=args.height, seconds=args.seconds)) if args.url else None
        connection.request("POST" if args.url else "GET", "/jobs" if args.url else "/status",
                           body=payload, headers={"Authorization": f"Bearer {token}",
                                                  "Content-Type": "application/json"})
        response = connection.getresponse()
        result = json.loads(response.read(4096))
        connection.close()
        print(json.dumps(result, ensure_ascii=False))
        if response.status >= 400 or result.get("state") == "failed":
            parser.exit(1)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Local bridge unavailable: {error}\n")


if __name__ == "__main__":
    main()
