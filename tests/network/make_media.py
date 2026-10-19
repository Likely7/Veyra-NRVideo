#!/usr/bin/env python3
"""Builds the media the #48 network tests serve (originals are only read, never modified).

  big-4k-av.mkv / big-4k-av.mp4   remux (-c:v copy) of an existing 4K30 ~70 Mbps clip plus a generated AAC track:
                                   the "huge high-bitrate MKV" a slow server cannot keep up with
  av-720p30-60s.mp4 / .mkv         small A/V files (synthetic: running timer + one beep per second)
  av-720p30-10min.ts               10 minutes of MPEG-TS for the packet-loss runs
  av-720p30-60s-subs.mkv           the 60 s MKV with two embedded SRT tracks (subtitle scan of a NAS-style file)
  live-src.ts                      20 s loop source for the real-time /live.ts relay
  av-1080i25-60s.ts                one minute of interlaced 1080i25 MPEG-TS (IPTV-like) for the damaged-interlaced run
  hls/vod, hls/multi               HLS playlists + segments (single rendition and three renditions)

usage: py -3.11 -I make_media.py --out <dir> --ffmpeg <ffmpeg.exe> --big-source <4k mp4> [--skip-big]
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def run(cmd):
    print(' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True)
    p.add_argument('--ffmpeg', required=True)
    p.add_argument('--big-source', default='')
    p.add_argument('--skip-big', action='store_true')
    a = p.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    ff = a.ffmpeg
    common = [ff, '-y', '-hide_banner', '-loglevel', 'error', '-nostdin']

    if not a.skip_big and a.big_source:
        for name, extra in (('big-4k-av.mkv', ['-f', 'matroska']), ('big-4k-av.mp4', ['-movflags', '+faststart'])):
            target = out / name
            if target.exists():
                continue
            run(common + ['-i', a.big_source, '-f', 'lavfi', '-i', 'sine=frequency=440:beep_factor=4:sample_rate=48000',
                          '-map', '0:v:0', '-map', '1:a:0', '-c:v', 'copy', '-c:a', 'aac', '-b:a', '128k', '-shortest'] + extra + [target])

    synthetic = ['-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30',
                 '-f', 'lavfi', '-i', 'sine=frequency=1000:beep_factor=4:sample_rate=48000']
    encode = ['-c:v', 'libx264', '-preset', 'veryfast', '-b:v', '3M', '-maxrate', '4M', '-bufsize', '6M', '-g', '60',
              '-keyint_min', '60', '-sc_threshold', '0', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '128k']
    for name, extra in (('av-720p30-60s.mp4', ['-movflags', '+faststart']), ('av-720p30-60s.mkv', [])):
        if not (out / name).exists():
            run(common + synthetic + ['-t', '60'] + encode + extra + [out / name])
    subs = out / 'av-720p30-60s-subs.mkv'
    if not subs.exists() and (out / 'av-720p30-60s.mkv').exists():
        for tag, lang in (('a', 'eng'), ('b', 'chi')):
            lines = [f'{n + 1}\n00:00:{n:02d},100 --> 00:00:{n:02d},900\nline {n + 1} ({lang})\n' for n in range(60)]
            (out / f'subs-{tag}.srt').write_text('\n'.join(lines), encoding='utf-8')
        run(common + ['-i', out / 'av-720p30-60s.mkv', '-i', out / 'subs-a.srt', '-i', out / 'subs-b.srt',
                      '-map', '0', '-map', '1', '-map', '2', '-c', 'copy', '-c:s', 'srt',
                      '-metadata:s:s:0', 'language=eng', '-metadata:s:s:1', 'language=chi', subs])
    if not (out / 'av-720p30-10min.ts').exists():
        run(common + synthetic + ['-t', '600'] + encode + ['-f', 'mpegts', out / 'av-720p30-10min.ts'])
    if not (out / 'live-src.ts').exists():
        run(common + synthetic + ['-t', '20'] + encode + ['-f', 'mpegts', out / 'live-src.ts'])
    if not (out / 'av-1080i25-60s.ts').exists():
        # IPTV-like 1080i25: two real fields per frame (50p folded by the interlace filter), interlaced x264 coding
        run(common + ['-f', 'lavfi', '-i', 'testsrc2=size=1920x1080:rate=50',
                      '-f', 'lavfi', '-i', 'sine=frequency=1000:beep_factor=4:sample_rate=48000', '-t', '60',
                      '-vf', 'interlace=scan=tff', '-c:v', 'libx264', '-preset', 'veryfast', '-b:v', '6M', '-maxrate', '8M',
                      '-bufsize', '12M', '-g', '50', '-keyint_min', '50', '-sc_threshold', '0', '-flags', '+ildct+ilme',
                      '-x264-params', 'tff=1', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '128k', '-f', 'mpegts',
                      out / 'av-1080i25-60s.ts'])

    hls = out / 'hls'
    vod = hls / 'vod'
    if not (vod / 'index.m3u8').exists():
        vod.mkdir(parents=True, exist_ok=True)
        run(common + ['-i', out / 'av-720p30-60s.mp4', '-c', 'copy', '-f', 'hls', '-hls_time', '4', '-hls_playlist_type', 'vod',
                      '-hls_segment_filename', vod / 'seg%03d.ts', vod / 'index.m3u8'])
    multi = hls / 'multi'
    if not (multi / 'master.m3u8').exists():
        multi.mkdir(parents=True, exist_ok=True)
        run(common + synthetic + ['-t', '40', '-filter_complex',
                                  '[0:v]split=3[a][b][c];[a]scale=640:360[v0];[b]scale=960:540[v1];[c]scale=1280:720[v2]',
                                  '-map', '[v0]', '-map', '[v1]', '-map', '[v2]', '-map', '1:a', '-map', '1:a', '-map', '1:a',
                                  '-c:v', 'libx264', '-preset', 'veryfast', '-g', '60', '-sc_threshold', '0', '-pix_fmt', 'yuv420p',
                                  '-b:v:0', '600k', '-b:v:1', '1400k', '-b:v:2', '3000k', '-c:a', 'aac', '-b:a', '96k',
                                  '-f', 'hls', '-hls_time', '4', '-hls_playlist_type', 'vod', '-master_pl_name', 'master.m3u8',
                                  '-var_stream_map', 'v:0,a:0 v:1,a:1 v:2,a:2',
                                  '-hls_segment_filename', multi / 'v%v_seg%03d.ts', multi / 'v%v.m3u8'])
    print('media ready:', out)


if __name__ == '__main__':
    main()
