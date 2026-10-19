#!/usr/bin/env python3
"""Local media server for the #48 network-playback tests (Python standard library only).

It stands in for the slow / unreliable servers the user does not have:

  * serves files below --root with HTTP Range support (progressive MP4/MKV, HLS playlists + segments);
  * --rate-mbps R        one shared link of R megabytes per second for every connection;
  * --stall-at S1,S2     at server second S every transfer freezes for --stall-seconds (connection stays open);
  * --outage-at S        at server second S active transfers are reset and new connections are reset for
                         --outage-seconds (a cable pulled out);
  * --drop-after-mb M    every response is cut off (reset) after M megabytes of body;
  * --ts-flip RATIO      damages RATIO of the 188-byte packets of *.ts files (size preserved, so Range
                         requests stay consistent): transport-error bit + garbled payload;
  * --live NAME=FILE     /NAME is an endless real-time MPEG-TS stream relayed from ffmpeg looping FILE
                         (no Content-Length); --ts-drop RATIO removes that fraction of its TS packets
                         (IPTV-style packet loss, produces continuity errors);
  * --ts-drop-file RATIO a *.ts file is served like a live stream (200, no Content-Length, no Range, closes at
                         the end) from its start with RATIO of its TS packets removed, as fast as the link allows;
  * /__stats             JSON: connections, in-flight requests, bytes sent, per-request log.

Everything is logged to --log (one JSON object per line) and summarised at /__stats.
"""
import argparse
import http.server
import json
import mimetypes
import os
import queue
import random
import socket
import socketserver
import struct
import subprocess
import sys
import threading
import time
from urllib.parse import unquote, urlparse

TS = 188
CHUNK = 64 * 1024


class Shaper:
    """One shared link: global rate limit, scheduled stalls and an outage window."""

    def __init__(self, args):
        self.rate = args.rate_mbps * 1048576.0 if args.rate_mbps > 0 else 0.0
        self.stalls = [(float(s), float(args.stall_seconds)) for s in args.stall_at.split(',') if s]
        self.outage = (float(args.outage_at), float(args.outage_seconds)) if args.outage_at >= 0 else None
        self.t0 = time.monotonic()
        self.lock = threading.Lock()
        self.tokens = 0.0
        self.last = time.monotonic()
        self.generation = 0          # bumped when an outage starts: in-flight transfers see it and reset
        self._outage_started = False

    def now(self):
        return time.monotonic() - self.t0

    def stalled(self):
        t = self.now()
        return any(start <= t < start + length for start, length in self.stalls)

    def in_outage(self):
        if not self.outage:
            return False
        t = self.now()
        active = self.outage[0] <= t < self.outage[0] + self.outage[1]
        if active and not self._outage_started:
            self._outage_started = True
            self.generation += 1
        return active

    def wait(self, nbytes, stop):
        """Blocks until nbytes may go out. Returns False when the transfer must be reset."""
        gen = self.generation
        while not stop.is_set():
            if self.in_outage() or self.generation != gen:
                return False
            if self.stalled():
                time.sleep(0.05)
                continue
            if not self.rate:
                return True
            with self.lock:
                now = time.monotonic()
                self.tokens = min(self.tokens + (now - self.last) * self.rate, max(self.rate * 0.05, CHUNK))
                self.last = now
                if self.tokens >= nbytes:
                    self.tokens -= nbytes
                    return True
                need = (nbytes - self.tokens) / self.rate
            time.sleep(min(max(need, 0.001), 0.05))
        return False


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.connections = 0
        self.refused = 0
        self.in_flight = 0
        self.max_in_flight = 0
        self.bytes_sent = 0
        self.requests = []

    def begin(self, path, rng, client):
        with self.lock:
            rec = {'id': len(self.requests) + 1, 't': round(time.monotonic() - T0, 3), 'path': path, 'range': rng,
                   'client': client, 'bytes': 0, 'endT': None, 'result': 'open'}
            self.requests.append(rec)
            self.in_flight += 1
            self.max_in_flight = max(self.max_in_flight, self.in_flight)
            return rec

    def add(self, rec, n):
        with self.lock:
            rec['bytes'] += n
            self.bytes_sent += n

    def end(self, rec, result):
        with self.lock:
            rec['endT'] = round(time.monotonic() - T0, 3)
            rec['result'] = result
            self.in_flight -= 1
        log(dict(event='request-end', id=rec['id'], path=rec['path'], range=rec['range'], bytes=rec['bytes'], result=result))

    def snapshot(self):
        with self.lock:
            return dict(connections=self.connections, refused=self.refused, inFlight=self.in_flight,
                        maxInFlight=self.max_in_flight, bytesSent=self.bytes_sent,
                        requestCount=len(self.requests), requests=list(self.requests[-200:]),
                        serverSeconds=round(time.monotonic() - T0, 3))


T0 = time.monotonic()
ARGS = None
SHAPER = None
STATS = Stats()
LOG = None
LOG_LOCK = threading.Lock()
STOP = threading.Event()
LIVE = {}
ACTIVE = set()
ACTIVE_LOCK = threading.Lock()


def reset_all():
    """A cable pulled out: every open connection dies with a reset, even one blocked in a write."""
    with ACTIVE_LOCK:
        sockets = list(ACTIVE)
    for sock in sockets:
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
            sock.close()
        except OSError:
            pass


def outage_watcher():
    started = False
    while not STOP.is_set():
        if SHAPER.in_outage() and not started:
            started = True
            log(dict(event='outage-start'))
            reset_all()
        elif started and not SHAPER.in_outage():
            started = False
            log(dict(event='outage-end'))
        time.sleep(0.02)


def log(obj):
    obj['t'] = round(time.monotonic() - T0, 3)
    if LOG:
        with LOG_LOCK:
            LOG.write(json.dumps(obj, ensure_ascii=False) + '\n')
            LOG.flush()


def flip_ts(buf, base, ratio, seed):
    """Deterministically damages ratio of the TS packets in buf (which starts at absolute offset base)."""
    if ratio <= 0:
        return buf
    data = bytearray(buf)
    first = -(-base // TS) * TS            # first packet boundary at or after base
    for off in range(first - base, len(data) - TS + 1, TS):
        index = (base + off) // TS
        h = ((index * 2654435761) ^ seed) & 0xFFFFFFFF
        if h / 4294967296.0 < ratio:
            data[off + 1] |= 0x80          # transport_error_indicator
            for i in range(off + 12, off + TS, 7):
                data[i] ^= 0x5A
    return bytes(data)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    server_version = 'veyra-test/1'

    def log_message(self, fmt, *a):
        pass

    def send_response(self, code, message=None):
        log(dict(event='response', conn=getattr(self, 'conn_id', 0), code=code, path=self.path, range=self.headers.get('Range')))
        super().send_response(code, message)

    def setup(self):
        super().setup()
        with STATS.lock:
            STATS.connections += 1
            self.conn_id = STATS.connections
        log(dict(event='connection-open', conn=self.conn_id, peer=self.client_address[1]))
        with ACTIVE_LOCK:
            ACTIVE.add(self.connection)
        if SHAPER.in_outage():
            with STATS.lock:
                STATS.refused += 1
            self.reset()

    def finish(self):
        log(dict(event='connection-close', conn=getattr(self, 'conn_id', 0)))
        with ACTIVE_LOCK:
            ACTIVE.discard(self.connection)
        try:
            super().finish()
        except OSError:
            pass

    def reset(self):
        try:
            self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
            self.connection.close()
        except OSError:
            pass
        self.close_connection = True

    def do_HEAD(self):
        self.serve(True)

    def do_GET(self):
        self.serve(False)

    def serve(self, head):
        path = unquote(urlparse(self.path).path)
        if path == '/__stats':
            body = json.dumps(STATS.snapshot()).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path.lstrip('/') in LIVE:
            return self.serve_live(path.lstrip('/'))
        full = os.path.normpath(os.path.join(ARGS.root, path.lstrip('/')))
        if not full.startswith(os.path.normpath(ARGS.root)) or not os.path.isfile(full):
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        if ARGS.ts_drop_file > 0 and full.lower().endswith('.ts') and not head:
            return self.serve_lossy_file(path, full)
        size = os.path.getsize(full)
        start, end, status = 0, size - 1, 200
        rng = self.headers.get('Range')
        if rng and rng.startswith('bytes='):
            a, _, b = rng[6:].partition('-')
            try:
                start = int(a) if a else max(0, size - int(b))
                end = min(int(b), size - 1) if (a and b) else size - 1
            except ValueError:
                start, end = 0, size - 1
            if start >= size:
                self.send_response(416)
                self.send_header('Content-Range', f'bytes */{size}')
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            status = 206
        ctype = {'.m3u8': 'application/vnd.apple.mpegurl', '.ts': 'video/mp2t', '.mkv': 'video/x-matroska'}.get(
            os.path.splitext(full)[1].lower()) or mimetypes.guess_type(full)[0] or 'application/octet-stream'
        self.send_response(status)
        self.send_header('Content-Type', ctype)
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Length', str(end - start + 1))
        if status == 206:
            self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.end_headers()
        rec = STATS.begin(path, [start, end], self.client_address[1])
        if head:
            STATS.end(rec, 'head')
            return
        result = 'done'
        is_ts = full.lower().endswith('.ts')
        try:
            with open(full, 'rb') as f:
                f.seek(start)
                pos = start
                limit = ARGS.drop_after_mb * 1048576 if ARGS.drop_after_mb > 0 else None
                sent = 0
                while pos <= end:
                    n = min(CHUNK, end - pos + 1)
                    if limit is not None and sent + n > limit:
                        n = int(limit - sent)
                        if n <= 0:
                            result = 'dropped'
                            self.reset()
                            break
                    chunk = f.read(n)
                    if not chunk:
                        break
                    if is_ts and ARGS.ts_flip > 0:
                        chunk = flip_ts(chunk, pos, ARGS.ts_flip, ARGS.seed)
                    if not SHAPER.wait(len(chunk), STOP):
                        result = 'reset'
                        self.reset()
                        break
                    self.wfile.write(chunk)
                    pos += len(chunk)
                    sent += len(chunk)
                    STATS.add(rec, len(chunk))
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            result = 'client-closed'
            self.close_connection = True
        STATS.end(rec, result)

    def serve_lossy_file(self, path, full):
        """The file as a live-style stream with packet loss (no length, no ranges), at link speed."""
        self.send_response(200)
        self.send_header('Content-Type', 'video/mp2t')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Connection', 'close')
        self.end_headers()
        self.close_connection = True
        rec = STATS.begin(path, None, self.client_address[1])
        rnd = random.Random(ARGS.seed + rec['id'])
        result = 'done'
        try:
            with open(full, 'rb') as f:
                while True:
                    chunk = f.read(CHUNK - CHUNK % TS)
                    if not chunk:
                        break
                    kept = bytearray()
                    for off in range(0, len(chunk) - TS + 1, TS):
                        if rnd.random() >= ARGS.ts_drop_file:
                            kept += chunk[off:off + TS]
                    if not SHAPER.wait(len(kept), STOP):
                        result = 'reset'
                        self.reset()
                        break
                    self.wfile.write(bytes(kept))
                    STATS.add(rec, len(kept))
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            result = 'client-closed'
        STATS.end(rec, result)

    # ---- live MPEG-TS relay -------------------------------------------------------------
    def serve_live(self, name):
        feed = LIVE[name]
        q = feed.subscribe()
        self.send_response(200)
        self.send_header('Content-Type', 'video/mp2t')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Connection', 'close')
        self.end_headers()
        self.close_connection = True
        rec = STATS.begin('/' + name, None, self.client_address[1])
        rnd = random.Random(ARGS.seed + rec['id'])
        result = 'done'
        gen = SHAPER.generation
        try:
            while not STOP.is_set():
                try:
                    chunk = q.get(timeout=1.0)
                except queue.Empty:
                    continue
                if SHAPER.in_outage() or SHAPER.generation != gen:
                    result = 'reset'
                    self.reset()
                    break
                if SHAPER.stalled():
                    while SHAPER.stalled() and not STOP.is_set():
                        time.sleep(0.05)
                    # the stream moved on during the stall; a real stalled TCP link delivers the backlog
                if ARGS.ts_drop > 0:
                    kept = bytearray()
                    for off in range(0, len(chunk) - TS + 1, TS):
                        if rnd.random() >= ARGS.ts_drop:
                            kept += chunk[off:off + TS]
                    chunk = bytes(kept)
                self.wfile.write(chunk)
                STATS.add(rec, len(chunk))
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            result = 'client-closed'
        finally:
            feed.unsubscribe(q)
        STATS.end(rec, result)


class ControlHandler(http.server.BaseHTTPRequestHandler):
    """Statistics on a second port that no outage, stall or rate limit touches."""

    def log_message(self, fmt, *a):
        pass

    def do_GET(self):
        body = json.dumps(STATS.snapshot()).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class LiveFeed:
    """ffmpeg -re -stream_loop -1 -c copy -f mpegts -  ->  every subscriber's bounded queue."""

    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()
        self.queues = []
        ffmpeg = ARGS.ffmpeg
        cmd = [ffmpeg, '-nostdin', '-hide_banner', '-loglevel', 'error', '-re', '-stream_loop', '-1', '-i', path,
               '-c', 'copy', '-muxdelay', '0', '-f', 'mpegts', '-']
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        threading.Thread(target=self.pump, daemon=True).start()

    def pump(self):
        size = TS * 7 * 8     # 10528 bytes, a whole number of TS packets
        buf = b''
        while not STOP.is_set():
            data = self.proc.stdout.read(size)
            if not data:
                break
            buf += data
            usable = len(buf) // TS * TS
            chunk, buf = buf[:usable], buf[usable:]
            if not chunk:
                continue
            with self.lock:
                for q in self.queues:
                    try:
                        q.put_nowait(chunk)
                    except queue.Full:
                        pass      # a stalled client loses data, like a real overloaded link

    def subscribe(self):
        q = queue.Queue(maxsize=4000)
        with self.lock:
            self.queues.append(q)
        return q

    def unsubscribe(self, q):
        with self.lock:
            if q in self.queues:
                self.queues.remove(q)

    def close(self):
        try:
            self.proc.kill()
        except OSError:
            pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True
    request_queue_size = 64


def main():
    global ARGS, SHAPER, LOG
    p = argparse.ArgumentParser()
    p.add_argument('--root', required=True)
    p.add_argument('--port', type=int, default=0)
    p.add_argument('--port-file', default='')
    p.add_argument('--rate-mbps', type=float, default=0.0)
    p.add_argument('--stall-at', default='')
    p.add_argument('--stall-seconds', type=float, default=10.0)
    p.add_argument('--outage-at', type=float, default=-1.0)
    p.add_argument('--outage-seconds', type=float, default=8.0)
    p.add_argument('--drop-after-mb', type=float, default=0.0)
    p.add_argument('--ts-flip', type=float, default=0.0)
    p.add_argument('--ts-drop', type=float, default=0.0)
    p.add_argument('--ts-drop-file', type=float, default=0.0)
    p.add_argument('--live', action='append', default=[])
    p.add_argument('--ffmpeg', default=os.environ.get('FFMPEG', 'ffmpeg'))
    p.add_argument('--seed', type=int, default=48)
    p.add_argument('--log', default='')
    ARGS = p.parse_args()
    ARGS.root = os.path.abspath(ARGS.root)
    SHAPER = Shaper(ARGS)
    if ARGS.log:
        LOG = open(ARGS.log, 'w', encoding='utf8')
    for spec in ARGS.live:
        name, _, path = spec.partition('=')
        LIVE[name] = LiveFeed(os.path.join(ARGS.root, path) if not os.path.isabs(path) else path)
    if SHAPER.outage:
        threading.Thread(target=outage_watcher, daemon=True).start()
    server = Server(('127.0.0.1', ARGS.port), Handler)
    port = server.server_address[1]
    control = http.server.ThreadingHTTPServer(('127.0.0.1', 0), ControlHandler)
    threading.Thread(target=control.serve_forever, daemon=True).start()
    if ARGS.port_file:
        with open(ARGS.port_file + '.control', 'w') as f:
            f.write(str(control.server_address[1]))
        with open(ARGS.port_file, 'w') as f:
            f.write(str(port))
    log(dict(event='listening', port=port, args=vars(ARGS)))
    print(f'listening on http://127.0.0.1:{port}', flush=True)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        STOP.set()
        for feed in LIVE.values():
            feed.close()
        server.server_close()


if __name__ == '__main__':
    main()
