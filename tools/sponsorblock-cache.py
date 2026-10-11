# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only SponsorBlock client. API data remains CC BY-NC-SA 4.0."""
import hashlib
import json
import math
from pathlib import Path
import queue
import re
import threading
import time
from urllib.error import HTTPError
from urllib.request import Request, build_opener, HTTPRedirectHandler


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def fetch(identifier):
    # Hash-prefix API avoids sending the exact video ID to the service.
    prefix = hashlib.sha256(identifier.encode()).hexdigest()[:4]
    request = Request(f"https://sponsor.ajay.app/api/skipSegments/{prefix}?category=sponsor",
                      headers={"User-Agent": "Tube5-PS5/0.1 SponsorBlock read-only pilot"})
    try:
        with build_opener(NoRedirect).open(request, timeout=8) as response:
            body = response.read(131073)
            if len(body) > 131072:
                raise ValueError("Oversized SponsorBlock response")
            records = json.loads(body)
    except HTTPError as error:
        if error.code == 404:
            return []
        raise
    if not isinstance(records, list) or len(records) > 256:
        raise ValueError("Invalid SponsorBlock response")
    for record in records:
        if isinstance(record, dict) and record.get("videoID") == identifier:
            segments = record.get("segments")
            if not isinstance(segments, list) or len(segments) > 128:
                raise ValueError("Invalid SponsorBlock segments")
            return segments
    return []


def ranges_for(records, duration):
    if type(duration) not in (int, float) or not math.isfinite(duration) or not 0 < duration <= 3600:
        raise ValueError("Invalid media duration")
    if not isinstance(records, list) or len(records) > 128:
        raise ValueError("Invalid SponsorBlock records")
    intervals = []
    mismatch = False
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("Invalid SponsorBlock record")
        if record.get("category") != "sponsor" or record.get("actionType") != "skip":
            continue
        source_duration = record.get("videoDuration", 0)
        if type(source_duration) not in (int, float) or not math.isfinite(source_duration):
            raise ValueError("Invalid source duration")
        if source_duration and abs(source_duration - duration) > 2:
            mismatch = True
            continue
        pair = record.get("segment")
        if (not isinstance(pair, list) or len(pair) != 2 or
                any(type(v) not in (int, float) or not math.isfinite(v) for v in pair)):
            raise ValueError("Invalid interval")
        start, end = pair
        if not 0 <= start < end <= 3600 or end > duration + 2:
            raise ValueError("Interval outside media")
        votes = record.get("votes", 0)
        if type(votes) not in (int, float) or not math.isfinite(votes) or votes < 0:
            continue
        intervals.append([round(start * 1000), round(min(end, duration) * 1000)])
    # Any positive duration mismatch disables the whole set, not a subset of
    # possibly outdated marks in an edited video.
    if mismatch:
        return "unaligned", []
    merged = []
    for start, end in sorted(intervals):
        if start >= end:
            continue
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return ("ready" if merged else "none"), merged[:8]


class SponsorCache:
    def __init__(self, directory=None, fetcher=fetch):
        self.directory = directory or Path(__file__).resolve().parents[1] / ".local/sponsorblock"
        self.directory.mkdir(parents=True, exist_ok=True)
        self.fetcher = fetcher
        self.lock = threading.Lock()
        self.values = {}
        self.pending = set()
        self.queue = queue.Queue(maxsize=8)
        self.worker = threading.Thread(target=self.run, daemon=True)
        self.worker.start()

    def decorate(self, entries):
        result = []
        for entry in entries:
            item = dict(entry, sb_state="none", sb_ranges=[])
            identifier = entry.get("youtube", "-")
            if re.fullmatch(r"[A-Za-z0-9_-]{11}", identifier):
                if not entry.get("timeline_full", False):
                    item["sb_state"] = "unaligned"
                else:
                    value = self.lookup(identifier)
                    if value:
                        if value.get("failed"):
                            item["sb_state"] = "unavailable"
                        else:
                            try:
                                duration = entry.get("duration", entry["seconds"])
                                item["sb_state"], item["sb_ranges"] = ranges_for(value["records"], duration)
                            except (ValueError, TypeError, KeyError):
                                item["sb_state"] = "unavailable"
                    else:
                        item["sb_state"] = "pending"
            result.append(item)
        return result

    def lookup(self, identifier):
        with self.lock:
            value = self.values.get(identifier)
            if value is None:
                path = self.directory / f"{identifier}.json"
                try:
                    if path.stat().st_size <= 131072:
                        candidate = json.loads(path.read_text())
                        if (isinstance(candidate, dict) and type(candidate.get("at")) in (int, float)
                                and isinstance(candidate.get("records"), list)):
                            value = candidate
                except (OSError, ValueError):
                    pass
            if value and 0 <= time.time() - value["at"] < (300 if value.get("failed") else 86400):
                self.values[identifier] = value
                return value
            if identifier not in self.pending:
                try:
                    self.queue.put_nowait(identifier)
                    self.pending.add(identifier)
                except queue.Full:
                    pass
            return None

    def run(self):
        while True:
            identifier = self.queue.get()
            if identifier is None:
                return
            try:
                value = dict(at=time.time(), records=self.fetcher(identifier),
                             source="https://sponsor.ajay.app/", license="CC-BY-NC-SA-4.0")
                payload = json.dumps(value)
                if len(payload.encode()) > 131072:
                    raise ValueError("Cache exceeds limit")
                temporary = self.directory / f"{identifier}.tmp"
                temporary.write_text(payload)
                temporary.replace(self.directory / f"{identifier}.json")
            except Exception:
                value = dict(at=time.time(), failed=True)
            with self.lock:
                self.values[identifier] = value
                self.pending.discard(identifier)

    def close(self):
        # Discard queued work; an active request has an eight-second timeout.
        while True:
            try: self.queue.get_nowait()
            except queue.Empty: break
        self.queue.put(None)
        self.worker.join(timeout=10)
