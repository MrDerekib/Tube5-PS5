#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Development-only probe; never saves/prints signed URLs or visitor tokens.

Protocol reference: TeamNewPipe/NewPipeExtractor, YoutubeStreamHelper,
YoutubeParsingHelper and ClientsConstants (dev, inspected 2026-10-11).
No download, conversion, bridge or runtime dependency for Tube5.
"""
import argparse
import json
import re
import secrets
import urllib.error
import urllib.parse
import urllib.request

LIMIT = 1024 * 1024
AGENT = "com.google.visionos.youtube/1.04(RealityDevice17,1; U; CPU visionOS 26_6_0 like Mac OS X; ES)"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def post(opener, url, body):
    request = urllib.request.Request(url, json.dumps(body).encode(), {
        "User-Agent": AGENT, "Content-Type": "application/json",
        "X-Goog-Api-Format-Version": "2",
    })
    with opener.open(request, timeout=12) as response:
        data = response.read(LIMIT + 1)
        if len(data) > LIMIT:
            raise ValueError("response exceeds 1 MiB")
        return json.loads(data)


def media_url(url):
    if not isinstance(url, str) or len(url) >= 8192:
        return False
    parsed = urllib.parse.urlsplit(url)
    return (parsed.scheme == "https" and parsed.hostname is not None
            and parsed.hostname.endswith(".googlevideo.com")
            and parsed.netloc == parsed.hostname
            and parsed.path == "/videoplayback" and bool(parsed.query)
            and not parsed.fragment and not re.search(r"[\s\\]", url))


def probe(video_id):
    if not re.fullmatch(r"[A-Za-z0-9_-]{11}", video_id):
        raise ValueError("expected an 11-character YouTube ID")
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    client = {"clientName": "VISIONOS", "clientVersion": "1.04",
              "clientScreen": "WATCH", "platform": "MOBILE", "deviceMake": "Apple",
              "deviceModel": "RealityDevice17,1", "osName": "visionOS",
              "osVersion": "26.6.0.23O770", "hl": "es", "gl": "ES", "utcOffsetMinutes": 0}
    body = {"context": {"client": client,
                        "request": {"internalExperimentFlags": [], "useSsl": True},
                        "user": {"lockedSafetyMode": False}}}
    visitor = post(opener, "https://www.youtube.com/youtubei/v1/visitor_id?prettyPrint=false", body)
    client["visitorData"] = visitor["responseContext"]["visitorData"]
    cpn = secrets.token_urlsafe(12)
    body.update(videoId=video_id, cpn=cpn, contentCheckOk=True, racyCheckOk=True)
    target = ("https://youtubei.googleapis.com/youtubei/v1/player?prettyPrint=false&t="
              + secrets.token_urlsafe(9) + "&id=" + video_id)
    player = post(opener, target, body)
    if player.get("videoDetails", {}).get("videoId") != video_id:
        raise ValueError("player identity missing/mismatched: "
                         + str(player.get("playabilityStatus", {}).get("status")))
    streams = player.get("streamingData", {})
    formats = streams.get("formats", []) + streams.get("adaptiveFormats", [])
    print(json.dumps({"id": video_id, "profile": "VISIONOS", "formats": len(formats),
                      "direct": sum(bool(f.get("url")) for f in formats),
                      "hls": bool(streams.get("hlsManifestUrl")),
                      "dash": bool(streams.get("dashManifestUrl"))}))
    validated = 0
    for mime_prefix, codec, height in (("video/mp4", "avc1.", 720), ("audio/mp4", "mp4a.", 0)):
        candidates = [f for f in formats if f.get("mimeType", "").startswith(mime_prefix)
                      and codec in f.get("mimeType", "") and f.get("height", 0) == height
                      and media_url(f.get("url", ""))]
        if not candidates:
            print(json.dumps({"kind": mime_prefix, "height": height, "accessible_url": False}))
            continue
        # Prefer the default/original AAC track and a sensible bitrate, not the
        # first entry (often itag 139, low bitrate). No language guessing.
        if mime_prefix == "audio/mp4":
            candidates.sort(key=lambda f: (
                f.get("audioTrack", {}).get("audioIsDefault", False),
                f.get("bitrate", 0) <= 192000, f.get("bitrate", 0)), reverse=True)
        chosen = candidates[0]
        url = chosen["url"] + "&cpn=" + cpn
        req = urllib.request.Request(url, headers={"User-Agent": AGENT, "Range": "bytes=0-4095"})
        with opener.open(req, timeout=12) as response:
            data = response.read(4097)
            ok = (response.status == 206 and len(data) == 4096 and data[4:8] == b"ftyp"
                  and response.headers.get("Content-Range", "").startswith("bytes 0-4095/"))
            print(json.dumps({"itag": chosen.get("itag"), "height": height,
                              "http": response.status, "bytes": len(data), "mp4_range": ok}))
            if not ok:
                raise ValueError("media range validation failed")
            validated += 1
    if validated != 2:
        raise ValueError("no validated video/audio pair")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video_id", nargs="?", default="bHIhgxav9LY")
    args = parser.parse_args()
    try:
        probe(args.video_id)
    except urllib.error.HTTPError as error:
        raise SystemExit(f"Probe HTTP {error.code} (signed URL withheld)") from None
    except (urllib.error.URLError, ValueError, KeyError) as error:
        # Do not stringify transport exceptions: they can contain signed URLs.
        raise SystemExit(f"Probe failed: {type(error).__name__} (details withheld)") from None
