#!/usr/bin/env python3
"""The #48 test matrix: one run per class, no stress tests (every run <= 300 s).

  py -3.11 -I run_network_tests.py [--only name,name] [--skip-big] [--out <dir>]

Each scenario starts slow_server.py (or mediamtx for RTSP) as a child process, runs
veyra_network_buffer_tests against it, evaluates the thresholds below and writes
<out>/<scenario>.json; the run ends with <out>/summary.json and summary.md.

Thresholds (from the task):
  * 1 % damaged TS packets never stop playback (decode-speed pass over the 10 min file,
    real-time pass on the live relay)
  * an 8 s outage recovers within 5 s after the server is back (VOD: first byte served by the
    reconnect; live: the picture moves again)
  * a VOD uses one connection for the whole session
  * the packet cache stays within its configured cap
  * audio / video deviation < 40 ms (95th percentile of the engine's lateness measure)
  * a server slower than the stream pauses to buffer, with the reason in the message, and never
    ends in an error; the clock does not advance while it waits
  * RTSP opens a single session; a local file behaves as before (session on/off identical)
"""
import argparse
import json
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netlib import APP, BASE, FFMPEG, MEDIA, TASK, Server, run_exe  # noqa: E402

REASON = '服务器速度'            # the bandwidth sentence of the paused-buffering message
RESULTS = {}


def requests_of(server):
    return server.stats().get('requests', [])


def check(checks, name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': detail})
    return ok


def finish(name, out, checks, data):
    passed = all(c['ok'] for c in checks)
    record = {'scenario': name, 'pass': passed, 'checks': checks, 'data': data}
    (out / f'{name}.json').write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding='utf-8')
    RESULTS[name] = record
    print(('PASS ' if passed else 'FAIL ') + name, flush=True)
    for c in checks:
        print('   ' + ('ok   ' if c['ok'] else 'FAIL ') + c['name'] + (f"  [{c['detail']}]" if c['detail'] else ''), flush=True)
    return passed


def server_stats(server):
    s = server.stats()
    return {k: v for k, v in s.items() if k != 'requests'}


def timeline(run_dir):
    rows = []
    path = Path(run_dir) / 'timeline.csv'
    if not path.exists():
        return rows
    lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
    head = lines[0].split(',')
    for line in lines[1:]:
        parts = line.split(',', len(head) - 1)
        if len(parts) == len(head):
            rows.append(dict(zip(head, parts)))
    return rows


def standstills(rows, minimum=1.0):
    """[(start, end)] where the playback position stood still for at least `minimum` seconds."""
    out, start, last = [], None, None
    for r in rows:
        t, p = float(r['t']), float(r['position'])
        if last is None or p != last:
            if start is not None and t - start >= minimum:
                out.append((start, t))
            start, last = t, p
    return out


# ----------------------------------------------------------------------------------------- scenarios
def selftest(out, a):
    checks = []
    p = subprocess.run([str(APP / 'veyra_network_buffer_tests.exe'), 'selftest'], cwd=str(APP), capture_output=True, text=True, timeout=60)
    check(checks, 'selftest exit code 0', p.returncode == 0, p.stdout.strip()[-200:] or p.stderr.strip()[-200:])
    return finish('selftest', out, checks, {'stdout': p.stdout[-600:]})


def local_ab(out, a):
    checks, data = [], {}
    for name in ('av-720p30-60s.mp4', 'av-720p30-60s.mkv'):
        f = MEDIA / name
        rc1, direct, _ = run_exe('source', f, out / f'ab-{name}-direct', dict(hash=1, frames=900))
        rc2, session, _ = run_exe('source', f, out / f'ab-{name}-session', dict(hash=1, frames=900), env=dict(VEYRA_TEST_FORCE_DEMUX_SESSION=1))
        data[name] = {'direct': direct, 'session': session}
        check(checks, f'{name}: direct run ok', rc1 == 0 and direct and not direct['session'], f'rc={rc1}')
        check(checks, f'{name}: session run ok', rc2 == 0 and session and session['session'], f'rc={rc2}')
        if direct and session:
            check(checks, f'{name}: identical frames', direct['frames'] == session['frames'] == 900, f"{direct['frames']} vs {session['frames']}")
            check(checks, f'{name}: identical picture hash', direct['hash'] == session['hash'], f"{direct['hash']} vs {session['hash']}")
            check(checks, f'{name}: monotonic pts', direct['nonMonotonic'] == 0 and session['nonMonotonic'] == 0)
    return finish('local-ab', out, checks, data)


def local_engine(out, a):
    checks = []
    rc, res, sec = run_exe('engine', MEDIA / 'av-720p30-60s.mp4', out / 'local-engine', dict(seconds=12))
    check(checks, 'plays', rc == 0 and res and not res['failed'], f'rc={rc}')
    if res:
        check(checks, 'no network session for a local file', 'local' in res['states'] and 'playing' not in res['states'] and 'buffering' not in res['states'], str(res['states']))
        check(checks, 'frames presented', res['realPresented'] >= 250, str(res['realPresented']))
        check(checks, 'A/V lateness p95 < 40 ms', res['maxLateP95Ms'] < 40, f"{res['maxLateP95Ms']:.1f}")
    return finish('local-engine', out, checks, {'result': res})


def vod_fast(out, a):
    checks, data = [], {}
    with Server(out / 'vod-fast-srv') as s:
        rc, res, sec = run_exe('engine', s.url('av-720p30-60s.mp4'), out / 'vod-fast', dict(seconds=22))
        st = server_stats(s)
    data.update(result=res, server=st)
    check(checks, 'plays without failure', rc == 0 and res and not res['failed'], f'rc={rc}')
    if res:
        check(checks, 'first picture within 6 s', 0 <= res['firstFrameSeconds'] <= 6, f"{res['firstFrameSeconds']:.2f}")
        check(checks, 'one connection for the session', st['connections'] == 1 and st['requestCount'] == 1 and st['maxInFlight'] == 1,
              f"connections={st['connections']} requests={st['requestCount']}")
        check(checks, 'A/V lateness p95 < 40 ms', res['maxLateP95Ms'] < 40, f"{res['maxLateP95Ms']:.1f}")
        check(checks, 'cache within its cap', res['maxBufferedMiB'] <= res['capMiB'] * 1.05 + 8, f"{res['maxBufferedMiB']:.1f} of {res['capMiB']:.0f} MiB")
        check(checks, 'no buffering stall on a fast server', res['bufferingSeconds'] < 0.5, f"{res['bufferingSeconds']:.2f}")
    return finish('vod-fast', out, checks, data)


def vod_slow(out, a):
    checks, data = [], {}
    with Server(out / 'vod-slow-srv', rate_mbps=0.2) as s:
        rc, res, sec = run_exe('engine', s.url('av-720p30-60s.mp4'), out / 'vod-slow', dict(seconds=45))
        st = server_stats(s)
    data.update(result=res, server=st)
    check(checks, 'never ends in an error', rc == 0 and res and not res['failed'], f'rc={rc}')
    if res:
        check(checks, 'pauses to buffer', res['bufferingSeconds'] + res['prebufferSeconds'] > 5, f"buffering={res['bufferingSeconds']:.1f}s prebuffer={res['prebufferSeconds']:.1f}s")
        check(checks, 'reason is given', any(REASON in m for m in res['messages']), str(res['messages'][:4]))
        check(checks, 'flagged as server slower than the stream', res['bandwidthLow'], f"server={res['serverMBps']:.2f} bitrate={res['bitrateMBps']:.2f} MB/s")
        check(checks, 'clock stands still while waiting', res['maxClockAdvanceWhileBuffering'] < 0.1, f"{res['maxClockAdvanceWhileBuffering']:.3f} s")
        check(checks, 'one connection for the session', st['connections'] == 1 and st['maxInFlight'] == 1, f"connections={st['connections']}")
        check(checks, 'cache within its cap', res['maxBufferedMiB'] <= res['capMiB'] * 1.05 + 8, f"{res['maxBufferedMiB']:.1f} of {res['capMiB']:.0f} MiB")
    return finish('vod-slow', out, checks, data)


def vod_big_4k(out, a):
    checks, data = [], {}
    if a.skip_big or not (MEDIA / 'big-4k-av.mkv').exists():
        checks.append({'name': 'skipped', 'ok': True, 'detail': 'no big file or --skip-big'})
        return finish('vod-big-4k-mkv', out, checks, {})
    with Server(out / 'vod-big-srv', rate_mbps=6.0) as s:
        rc, res, sec = run_exe('engine', s.url('big-4k-av.mkv'), out / 'vod-big', dict(seconds=100))
        st = server_stats(s)
    data.update(result=res, server=st)
    check(checks, 'never ends in an error', rc == 0 and res and not res['failed'], f'rc={rc}')
    if res:
        check(checks, 'flagged as server slower than the stream', res['bandwidthLow'], f"server={res['serverMBps']:.2f} bitrate={res['bitrateMBps']:.2f} MB/s")
        check(checks, 'reason is given', any(REASON in m for m in res['messages']), str(res['messages'][:4]))
        check(checks, 'pauses to buffer', res['bufferingSeconds'] + res['prebufferSeconds'] > 5, f"{res['bufferingSeconds'] + res['prebufferSeconds']:.1f} s")
        check(checks, 'plays between the pauses', res['realPresented'] > 30, str(res['realPresented']))
        check(checks, 'clock stands still while waiting', res['maxClockAdvanceWhileBuffering'] < 0.1, f"{res['maxClockAdvanceWhileBuffering']:.3f} s")
        # a stop interrupts FFmpeg inside a Matroska block, its resync then starts one more TCP connect that
        # is cancelled before it sends anything (seen in FFmpeg's own log): not a second reader
        check(checks, 'one connection for the session', st['requestCount'] == 1 and st['maxInFlight'] == 1 and st['connections'] <= 2,
              f"requests={st['requestCount']} in flight<={st['maxInFlight']} tcp connections={st['connections']} (the 2nd is the stop-time resync attempt)")
        check(checks, 'cache within its cap (auto: <= 1 GiB when the server is slow)', res['maxBufferedMiB'] <= max(res['capMiB'], 1) * 1.05 + 16 and res['maxBufferedMiB'] <= 1100,
              f"{res['maxBufferedMiB']:.0f} of {res['capMiB']:.0f} MiB")
    return finish('vod-big-4k-mkv', out, checks, data)


def vod_stall(out, a):
    checks, data = [], {}
    # 0.6 MB/s is above the 0.4 MB/s stream, so the small window fills; the 10 s stall then drains it.
    with Server(out / 'vod-stall-srv', rate_mbps=0.6, stall_at='6', stall_seconds=10) as s:
        rc, res, sec = run_exe('engine', s.url('av-720p30-60s.mp4'), out / 'vod-stall', dict(seconds=50, level='small'))
        st = server_stats(s)
    data.update(result=res, server=st)
    check(checks, 'never ends in an error', rc == 0 and res and not res['failed'], f'rc={rc}')
    if res:
        check(checks, 'paused to buffer during the stall', res['stalls'] >= 1 and res['bufferingSeconds'] > 3, f"stalls={res['stalls']} buffering={res['bufferingSeconds']:.1f}s")
        check(checks, 'playing again afterwards', res['states'].get('playing', 0) > 100 and res['finalPosition'] > 10, f"position={res['finalPosition']:.1f}")
        check(checks, 'clock stands still while waiting', res['maxClockAdvanceWhileBuffering'] < 0.1, f"{res['maxClockAdvanceWhileBuffering']:.3f} s")
        check(checks, 'one connection (a stall is not a reconnect)', st['connections'] == 1, f"connections={st['connections']} reconnects={res['reconnects']}")
    return finish('vod-stall', out, checks, data)


def vod_outage(out, a):
    checks, data = [], {}
    f = MEDIA / 'av-720p30-60s.mp4'
    rc0, base, _ = run_exe('source', f, out / 'vod-outage-base', dict(hash=1))
    with Server(out / 'vod-outage-srv', rate_mbps=1.5, outage_at=4, outage_seconds=8) as s:
        rc, res, sec = run_exe('source', s.url('av-720p30-60s.mp4'), out / 'vod-outage', dict(hash=1, level='small'))
        st = server_stats(s)
        reqs = requests_of(s)
    outage_end = 4 + 8
    served = [r for r in reqs if r['t'] >= outage_end - 0.05 and r['bytes'] > 0]
    delay = (served[0]['t'] - outage_end) if served else None
    data.update(baseline=base, result=res, server=st, firstServedAfterOutage=served[0] if served else None, recoveryDelaySeconds=delay)
    check(checks, 'plays to the end', rc == 0 and res and res['status'] == 'eos', f"rc={rc}")
    if res and base:
        check(checks, 'reconnected', res['reconnects'] >= 1, f"reconnects={res['reconnects']}")
        check(checks, 'recovery within 5 s after the server is back', delay is not None and delay <= 5, f'{delay}')
        check(checks, 'frames identical to the undisturbed run', res['frames'] == base['frames'] and res['hash'] == base['hash'],
              f"{res['frames']} vs {base['frames']} frames, hash {'equal' if res['hash'] == base['hash'] else 'DIFFERENT'}")
        check(checks, 'no timeline break', res['nonMonotonic'] == 0 and res['discontinuities'] == 0, f"nonMonotonic={res['nonMonotonic']} discontinuities={res['discontinuities']}")
    return finish('vod-outage-8s', out, checks, data)


def vod_reconnect_off(out, a):
    checks, data = [], {}
    with Server(out / 'vod-noreconnect-srv', rate_mbps=1.5, outage_at=3, outage_seconds=8) as s:
        rc, res, sec = run_exe('engine', s.url('av-720p30-60s.mp4'), out / 'vod-noreconnect', dict(seconds=40, level='small', reconnect=0))
    data.update(result=res)
    check(checks, 'stops with a reason instead of freezing', res is not None and res['failed'], f"rc={rc}")
    if res:
        check(checks, 'the reason says auto-reconnect is off', '自动重连已关闭' in res['failure'], res['failure'])
        check(checks, 'no reconnect attempted', res['reconnects'] == 0 and res['connections'] == 1, f"reconnects={res['reconnects']} connections={res['connections']}")
    return finish('vod-reconnect-off', out, checks, data)


def live_outage(out, a):
    checks, data = [], {}
    with Server(out / 'live-outage-srv', live=['live.ts=live-src.ts'], outage_at=10, outage_seconds=8) as s:
        rc, res, sec = run_exe('engine', s.url('live.ts'), out / 'live-outage', dict(seconds=45))
        st = server_stats(s)
        reqs = requests_of(s)
    rows = timeline(out / 'live-outage')
    first = min((r['t'] for r in reqs), default=0)
    outage_end_engine = (10 + 8) - first + 0.3
    stands = [x for x in standstills(rows, 1.5) if x[1] > outage_end_engine - 2]
    resume = stands[0][1] - outage_end_engine if stands else None
    data.update(result=res, server=st, standstills=stands, recoverySeconds=resume)
    check(checks, 'no failure', rc == 0 and res and not res['failed'], f'rc={rc}')
    if res:
        check(checks, 'reconnected', res['reconnects'] >= 1, f"reconnects={res['reconnects']}")
        check(checks, 'picture moves again within 5 s after the server is back', resume is not None and resume <= 5, f'{resume}')
        check(checks, 'A/V lateness p95 < 40 ms', res['maxLateP95Ms'] < 40, f"{res['maxLateP95Ms']:.1f}")
        check(checks, 'plays on afterwards', res['finalPosition'] > 25, f"position={res['finalPosition']:.1f}")
    return finish('live-outage-8s', out, checks, data)


def _ts_decode(out, name, flags, what):
    checks, data = [], {}
    with Server(out / f'{name}-srv', **flags) as s:
        rc, res, sec = run_exe('source', s.url('av-720p30-10min.ts'), out / name, dict(tolerant=1), timeout=290)
        st = server_stats(s)
    data.update(result=res, server=st, wallSeconds=sec)
    check(checks, f'plays the whole 10 minutes at decode speed ({what})', rc == 0 and res and res['status'] == 'eos' and res['lastPts'] > 585,
          f"rc={rc} lastPts={res and res['lastPts']:.1f} wall={sec:.0f}s")
    if res:
        check(checks, 'damage was really injected and noticed', res['corruptPackets'] > 100, f"packets={res['corruptPackets']} frames={res['corruptFrames']}")
        check(checks, 'about 18000 pictures (1 % damage may drop a few)', 16500 <= res['frames'] <= 18100, str(res['frames']))
        check(checks, 'no error, no reconnect', res['status'] != 'error' and not res['error'] and res['reconnects'] == 0, f"{res['error']} reconnects={res['reconnects']}")
    return finish(name, out, checks, data)


def ts_loss_decode(out, a):
    # bit errors in 1 % of the packets of the file, served as an ordinary seekable VOD
    return _ts_decode(out, 'ts-flip-1pct-10min-decode', dict(ts_flip=0.01), '1 % damaged packets, VOD file')


def ts_drop_decode(out, a):
    """1 % of the packets removed (continuity errors), served as a live-style stream with no length. At the end of the
    10 minutes the server closes and the stream starts over (its timestamps jump back by 600 s): 20000 pictures take
    the run 2000 pictures into the second pass, so a stall at the restart would show."""
    checks, data = [], {}
    name = 'ts-drop-1pct-10min-decode'
    with Server(out / f'{name}-srv', ts_drop_file=0.01) as s:
        rc, res, sec = run_exe('source', s.url('av-720p30-10min.ts'), out / name, dict(tolerant=1, frames=20000, seconds=250, ffmpeglog='quiet'), timeout=290)
        st = server_stats(s)
    data.update(result=res, server=st, wallSeconds=sec)
    check(checks, 'plays 20000 pictures (the 10 minutes and the restarted stream behind them) without stopping', rc == 0 and res and res['status'] == 'limit' and res['frames'] == 20000,
          f"rc={rc} frames={res and res['frames']} status={res and res['status']} wall={sec:.0f}s")
    if res:
        check(checks, 'damage was really injected and noticed', res['corruptPackets'] > 100, f"packets={res['corruptPackets']} frames={res['corruptFrames']}")
        check(checks, 'the restart is absorbed: one continuous timeline, flagged once', res['nonMonotonic'] == 0 and res['discontinuities'] == 1 and res['lastPts'] > 650,
              f"lastPts={res['lastPts']:.1f} nonMonotonic={res['nonMonotonic']} discontinuities={res['discontinuities']}")
        check(checks, 'no gap in the pictures longer than 1 s', res['maxFrameGapMs'] < 1000, f"{res['maxFrameGapMs']:.0f} ms")
        check(checks, 'no error', res['status'] != 'error' and not res['error'], res['error'])
    return finish(name, out, checks, data)


def ts_flip_interlaced(out, a):
    """1 % damaged packets in an interlaced 1080i25 TS (IPTV-like) with the player's deinterlacing on: damaged
    pictures on the interlaced path follow the same policy as progressive ones instead of stopping the source."""
    checks, data = [], {}
    name = 'ts-flip-1pct-interlaced'
    with Server(out / f'{name}-srv', ts_flip=0.01) as s:
        rc, res, sec = run_exe('source', s.url('av-1080i25-60s.ts'), out / name, dict(tolerant=1, deinterlace=1), timeout=290)
        st = server_stats(s)
    data.update(result=res, server=st, wallSeconds=sec)
    check(checks, 'plays the whole minute through the deinterlacer', rc == 0 and res and res['status'] == 'eos' and res['lastPts'] > 58,
          f"rc={rc} status={res and res['status']} lastPts={res and res['lastPts']} error={res and res['error']}")
    if res:
        check(checks, 'the deinterlacer was in use', res.get('deinterlaced') is True, str(res.get('deinterlaced')))
        check(checks, 'damage was really injected and noticed', res['corruptPackets'] + res['corruptFrames'] > 10,
              f"packets={res['corruptPackets']} frames={res['corruptFrames']}")
        check(checks, 'about 3000 fields (1 % damage may drop a few)', 2700 <= res['frames'] <= 3010, str(res['frames']))
        check(checks, 'no error, no reconnect', res['status'] != 'error' and not res['error'] and res['reconnects'] == 0,
              f"{res['error']} reconnects={res['reconnects']}")
    return finish(name, out, checks, data)


def live_restart(out, a):
    """A live stream that starts over (timestamps jump back) every 20 s: the picture and the sound go on."""
    checks, data = [], {}
    with Server(out / 'live-restart-srv', ts_drop_file=0.00001) as s:
        rc, res, sec = run_exe('engine', s.url('live-src.ts'), out / 'live-restart', dict(seconds=65, ffmpeglog='quiet'), timeout=200)
        st = server_stats(s)
    rows = timeline(out / 'live-restart')
    data.update(result=res, server=st)
    check(checks, 'no failure', rc == 0 and res and not res['failed'], f'rc={rc}')
    if res:
        stands = [x for x in standstills(rows, 1.0) if x[0] >= res['firstFrameSeconds'] - 0.1]
        worst = max((b - a_ for a_, b in stands), default=0.0)
        data['standstillsAfterFirstPicture'] = stands
        check(checks, 'the stream restarted (new connection per pass)', st['connections'] >= 3, f"connections={st['connections']}")
        check(checks, 'one continuous timeline across the restarts', res['finalPosition'] > 60, f"position={res['finalPosition']:.1f}")
        check(checks, 'picture never stands still for 1.5 s', worst < 1.5, f'worst {worst:.2f} s')
        check(checks, 'no buffering pause', res['bufferingSeconds'] < 1.0 and res['stalls'] == 0, f"buffering={res['bufferingSeconds']:.2f}s stalls={res['stalls']}")
        check(checks, 'A/V lateness p95 < 40 ms', res['maxLateP95Ms'] < 40, f"{res['maxLateP95Ms']:.1f}")
    return finish('live-restart', out, checks, data)


def ts_loss_realtime(out, a):
    checks, data = [], {}
    with Server(out / 'ts-loss-rt-srv', live=['live.ts=live-src.ts'], ts_drop=0.01) as s:
        rc, res, sec = run_exe('engine', s.url('live.ts'), out / 'ts-loss-rt', dict(seconds=200), timeout=290)
        st = server_stats(s)
    data.update(result=res, server=st)
    check(checks, 'never stops (200 s real time)', rc == 0 and res and not res['failed'] and res['finalPosition'] > 150, f"rc={rc} position={res and res['finalPosition']:.1f}")
    if res:
        check(checks, 'damage noticed', res['corruptPackets'] > 50, f"packets={res['corruptPackets']}")
        # the opening (first picture) is not a freeze: only standstills after the first picture count
        rows = timeline(out / 'ts-loss-rt')
        stands = [x for x in standstills(rows, 1.0) if x[0] >= res['firstFrameSeconds'] - 0.1]
        worst = max((b - a for a, b in stands), default=0.0)
        data['standstillsAfterFirstPicture'] = stands
        check(checks, 'picture never stands still for 1.5 s after the first picture', worst < 1.5, f"worst {worst:.2f} s, first picture at {res['firstFrameSeconds']:.2f} s")
        check(checks, 'A/V lateness p95 < 40 ms', res['maxLateP95Ms'] < 40, f"{res['maxLateP95Ms']:.1f}")
        check(checks, 'no reconnect needed for 1 % loss', res['reconnects'] == 0, f"{res['reconnects']}")
    return finish('ts-loss-1pct-realtime', out, checks, data)


def stop_latency(out, a):
    """A user stop / source switch must get out of a blocked connect, read or wait at once."""
    checks, data = [], {}
    cases = (
        ('reconnecting', dict(live=['live.ts=live-src.ts'], outage_at=6, outage_seconds=40), 'live.ts', dict(seconds=14), ('reconnecting', 'buffering')),
        ('buffering', dict(rate_mbps=0.2), 'av-720p30-60s.mp4', dict(seconds=14), ('buffering', 'prebuffering')),
        ('opening', dict(stall_at='0', stall_seconds=60), 'av-720p30-60s.mp4', dict(seconds=8), ('prebuffering', 'idle', 'local')),
    )
    for name, flags, target, opts, states in cases:
        with Server(out / f'stop-{name}-srv', **flags) as s:
            rc, res, sec = run_exe('engine', s.url(target), out / f'stop-{name}', opts, timeout=120)
        data[name] = res
        if not res:
            check(checks, f'{name}: ran', False, f'rc={rc}')
            continue
        check(checks, f'{name}: stopped while {res["endState"]}', res['endState'] in states, res['endState'])
        check(checks, f'{name}: engine torn down in < 1.5 s', res['teardownMs'] < 1500, f"{res['teardownMs']:.0f} ms")
    return finish('stop-latency', out, checks, data)


def hls(out, a):
    checks, data = [], {}
    for which, path in (('vod', 'hls/vod/index.m3u8'), ('multi', 'hls/multi/master.m3u8')):
        if not (MEDIA / path).exists():
            continue
        with Server(out / f'hls-{which}-srv') as s:
            rc, res, sec = run_exe('source', s.url(path), out / f'hls-{which}', dict(seconds=60, frames=600))
            st = server_stats(s)
            paths = [r['path'] for r in requests_of(s)]
        segments = [p.rsplit('/', 1)[-1] for p in paths if p.endswith('.ts')]
        perVariant = {}
        for name in segments:
            perVariant[name.split('_')[0]] = perVariant.get(name.split('_')[0], 0) + 1
        data[which] = {'result': res, 'server': st, 'segmentsPerVariant': perVariant}
        check(checks, f'hls {which}: plays', rc == 0 and res and res['frames'] >= 300, f"rc={rc} frames={res and res['frames']}")
        if which == 'multi':
            # FFmpeg's HLS demuxer opens the first segments of every variant while reading the header; after that
            # only the best variant (v2) is downloaded because the session drops the other streams
            best = max(perVariant, key=perVariant.get) if perVariant else None
            check(checks, 'hls multi: the best variant carries the stream, the others stop after the header', best == 'v2' and perVariant.get('v2', 0) >= 8
                  and all(n <= 2 for v, n in perVariant.items() if v != 'v2'), f'segments per variant {perVariant}')
    return finish('hls', out, checks, data)


def rtsp(out, a):
    checks, data = [], {}
    mtx = BASE / 'tmp/issues41-45-20261016/mediamtx/mediamtx.exe'
    if not mtx.exists():
        checks.append({'name': 'skipped', 'ok': True, 'detail': 'mediamtx not found'})
        return finish('rtsp', out, checks, {})
    work = out / 'rtsp-mtx'
    work.mkdir(parents=True, exist_ok=True)

    def port():
        with socket.socket() as c:
            c.bind(('127.0.0.1', 0))
            return c.getsockname()[1]
    rport, aport = port(), port()
    cfg = work / 'mediamtx.yml'
    cfg.write_text(f'logLevel: info\nrtsp: true\nrtspAddress: 127.0.0.1:{rport}\nrtspTransports: [tcp]\nrtmp: false\nhls: false\n'
                   f'webrtc: false\nsrt: false\napi: true\napiAddress: 127.0.0.1:{aport}\nplayback: false\npaths:\n  test:\n', encoding='utf-8')
    procs = []
    try:
        procs.append(subprocess.Popen([str(mtx), str(cfg)], cwd=str(work), stdout=(work / 'mtx.log').open('wb'), stderr=subprocess.STDOUT, creationflags=0x08000000))
        time.sleep(0.8)
        procs.append(subprocess.Popen([str(FFMPEG), '-nostdin', '-hide_banner', '-loglevel', 'error', '-re', '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30',
                                       '-re', '-f', 'lavfi', '-i', 'sine=frequency=1000:sample_rate=48000', '-c:v', 'libx264', '-preset', 'veryfast', '-g', '60',
                                       '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-f', 'rtsp', '-rtsp_transport', 'tcp', f'rtsp://127.0.0.1:{rport}/test'],
                                      cwd=str(work), stdout=(work / 'push.log').open('wb'), stderr=subprocess.STDOUT, creationflags=0x08000000))
        time.sleep(2.0)
        rc, res, sec = run_exe('engine', f'rtsp://127.0.0.1:{rport}/test', out / 'rtsp', dict(seconds=22))
        sessions = []
        for _ in range(3):
            try:
                body = json.loads(urllib.request.urlopen(f'http://127.0.0.1:{aport}/v3/rtspsessions/list', timeout=3).read())
                sessions = body.get('items', [])
                break
            except Exception:
                time.sleep(0.3)
        log = (work / 'mtx.log').read_text(errors='replace')
        opened = log.count('is reading from path')
        data.update(result=res, mediamtxReaders=opened, sessionsWhileRunning=len(sessions))
    finally:
        for p in procs:
            if p.poll() is None:
                p.terminate()
    check(checks, 'plays without failure', rc == 0 and res and not res['failed'], f'rc={rc}')
    check(checks, 'exactly one RTSP reader session opened (mediamtx log)', data.get('mediamtxReaders') == 1, f"readers={data.get('mediamtxReaders')}")
    if res:
        check(checks, 'picture moves', res['realPresented'] > 200, str(res['realPresented']))
        check(checks, 'A/V lateness p95 < 40 ms', res['maxLateP95Ms'] < 40, f"{res['maxLateP95Ms']:.1f}")
    return finish('rtsp', out, checks, data)


def nas_slow(out, a):
    checks, data = [], {}
    env = dict(VEYRA_TEST_FORCE_DEMUX_SESSION=1, VEYRA_TEST_SLOW_IO=0.2)
    rc, res, sec = run_exe('engine', MEDIA / 'av-720p30-60s.mp4', out / 'nas-slow', dict(seconds=40), env=env)
    data.update(result=res)
    check(checks, 'never ends in an error', rc == 0 and res and not res['failed'], f'rc={rc}')
    if res:
        check(checks, 'goes through the shared session', res['connections'] == 1 and res['states'].get('buffering', 0) + res['states'].get('prebuffering', 0) > 0, str(res['states']))
        check(checks, 'pauses to buffer with the reason', res['bufferingSeconds'] + res['prebufferSeconds'] > 5 and any(REASON in m for m in res['messages']),
              f"buffering={res['bufferingSeconds']:.1f}s {res['messages'][:2]}")
        check(checks, 'clock stands still while waiting', res['maxClockAdvanceWhileBuffering'] < 0.1, f"{res['maxClockAdvanceWhileBuffering']:.3f} s")
    return finish('nas-slow-io', out, checks, data)


def subtitle_scan(out, a):
    checks, data = [], {}
    f = MEDIA / 'av-720p30-60s-subs.mkv'
    paced_env = dict(VEYRA_TEST_FORCE_REMOTE_PATH=1, VEYRA_TEST_SLOW_IO=4)
    rc1, paced, _ = run_exe('scan', f, out / 'scan-paced', dict(closedSeconds=3), env=paced_env)
    rc2, toggle, _ = run_exe('scan', f, out / 'scan-toggle', dict(closedSeconds=1, toggle=1, closeAfter=1.0, closedAgainSeconds=2.0), env=paced_env)
    rc3, local, _ = run_exe('scan', f, out / 'scan-local', dict(closedSeconds=0.5))
    data.update(paced=paced, toggle=toggle, local=local)
    if paced:
        check(checks, 'server file: nothing read or published while playback is not buffered', paced['serverFile'] and paced['bytesWhileClosed'] == 0 and paced['publishedWhileClosed'] == 0)
        check(checks, 'server file: scan finishes once the gate opens', rc1 == 0 and paced['complete'] and paced['tracks'] == 2 and paced['cues'] == [60, 60], str(paced['cues']))
        check(checks, 'server file: paced (4 MB/s cap)', paced['rateMBps'] <= 4.4 and paced['scanSeconds'] >= 4.5, f"{paced['rateMBps']:.2f} MB/s over {paced['scanSeconds']:.1f} s")
    if toggle:
        check(checks, 'server file: reading stops while the gate is closed', toggle['complete'] and toggle['bytesWhileClosedAgain'] == 0, f"{toggle['bytesWhileClosedAgain']} bytes")
        check(checks, 'server file: ... and resumes to the end', toggle['bytesRead'] == toggle['fileBytes'] and toggle['cues'] == [60, 60])
    if local:
        check(checks, 'local file: scanned immediately as before', rc3 == 0 and not local['serverFile'] and local['complete'] and local['bytesRead'] == 0 and local['cues'] == [60, 60])
    return finish('subtitle-scan-nas', out, checks, data)


ORDER = [('selftest', selftest), ('local-ab', local_ab), ('local-engine', local_engine), ('vod-fast', vod_fast), ('vod-slow', vod_slow),
         ('vod-big-4k-mkv', vod_big_4k), ('vod-stall', vod_stall), ('vod-outage-8s', vod_outage), ('vod-reconnect-off', vod_reconnect_off),
         ('live-outage-8s', live_outage), ('stop-latency', stop_latency), ('ts-flip-1pct-10min-decode', ts_loss_decode), ('ts-drop-1pct-10min-decode', ts_drop_decode), ('ts-loss-1pct-realtime', ts_loss_realtime), ('ts-flip-1pct-interlaced', ts_flip_interlaced), ('live-restart', live_restart),
         ('hls', hls), ('rtsp', rtsp), ('nas-slow-io', nas_slow), ('subtitle-scan-nas', subtitle_scan)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', default='')
    ap.add_argument('--skip-big', action='store_true')
    ap.add_argument('--out', default='')
    a = ap.parse_args()
    out = Path(a.out) if a.out else BASE / 'tests' / TASK / 'results' / time.strftime('%Y%m%d-%H%M%S')
    out.mkdir(parents=True, exist_ok=True)
    only = {x for x in a.only.split(',') if x}
    for name, fn in ORDER:
        if only and name not in only:
            continue
        started = time.time()
        try:
            fn(out, a)
        except Exception as exc:   # a broken scenario is a failure, not a crash of the matrix
            RESULTS[name] = {'scenario': name, 'pass': False, 'checks': [{'name': 'ran', 'ok': False, 'detail': repr(exc)}], 'data': {}}
            print('FAIL ' + name + ' ' + repr(exc), flush=True)
        RESULTS[name]['seconds'] = round(time.time() - started, 1)
    summary = {'out': str(out), 'passed': [n for n, r in RESULTS.items() if r['pass']], 'failed': [n for n, r in RESULTS.items() if not r['pass']]}
    (out / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=1), encoding='utf-8')
    lines = ['| scenario | result | seconds |', '|---|---|---|']
    for n, r in RESULTS.items():
        lines.append(f"| {n} | {'PASS' if r['pass'] else 'FAIL'} | {r.get('seconds', '')} |")
    (out / 'summary.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('summary:', json.dumps(summary, ensure_ascii=False))
    return 0 if not summary['failed'] else 1


if __name__ == '__main__':
    sys.exit(main())
