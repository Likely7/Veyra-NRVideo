# Comparison helper for tools/dovi_p5_probe output.
#
#   python compare_raw.py pq <probe.raw> <reference.raw>
#       probe:     RGBA16F linear BT.709 scRGB (1.0 = 80 nits), as written by the probe
#       reference: 16-bit PQ BT.2020 RGB, e.g.
#           ffmpeg -ss <t> -i <file> -frames:v 1 \
#             -vf "libplacebo=apply_dolbyvision=1:color_trc=smpte2084:format=rgb48" \
#             -f rawvideo -pix_fmt rgb48le reference.raw
#   python compare_raw.py f2 <a.raw> <b.raw>
#       two probe runs (new shader vs pre-change shader, converted vs control, ...)
#   python compare_raw.py u2 <a.raw> <b.raw>
#       two 16-bit raw files
#
# The probe is 3840x2160 in every recorded run; set --width if that changes.
import sys

import numpy as np

W, H = 3840, 2160
M1 = 2610.0 / 16384.0
M2 = 2523.0 / 4096.0 * 128.0
C1 = 3424.0 / 4096.0
C2 = 2413.0 / 4096.0 * 32.0
C3 = 2392.0 / 4096.0 * 32.0
# The player's linear BT.2020 -> BT.709 matrix, i.e. the inverse of what the
# probe's scRGB output already carries.
M709 = np.array([[1.660491, -0.587641, -0.072850],
                 [-0.124550, 1.132900, -0.008349],
                 [-0.018151, -0.100579, 1.118730]], dtype=np.float32)
M709inv = np.linalg.inv(M709)


def pq_oetf(nits):
    y = np.clip(nits / 10000.0, 0, None)
    ym = np.power(y, M1)
    return np.power((C1 + C2 * ym) / (1.0 + C3 * ym), M2)


def load_probe(path):
    return np.fromfile(path, dtype='<f2').astype(np.float32).reshape(H, W, 4)[..., :3]


def load_ref_pq(path):
    return np.fromfile(path, dtype='<u2').astype(np.float32).reshape(H, W, 3) / 65535.0


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    mode, a_path, b_path = sys.argv[1], sys.argv[2], sys.argv[3]
    if mode == 'pq':
        probe = load_probe(a_path)
        ref = load_ref_pq(b_path)
        # scRGB -> absolute nits -> BT.2020 -> PQ, the domain the reference is in
        pq = pq_oetf((probe * 80.0) @ M709inv.T.astype(np.float32))
        e = np.abs(pq - ref)
        print(f'{a_path} vs {b_path} (PQ BT.2020)')
        print(f'  meanAbs={e.mean():.5f} p50={np.percentile(e, 50):.5f} '
              f'p95={np.percentile(e, 95):.5f} max={e.max():.4f}')
        bright = ref.mean(-1) > 0.4
        if bright.sum() > 1000:
            g, r = pq[bright].mean(0), ref[bright].mean(0)
            print(f'  bright-area PQ RGB probe={np.round(g, 4)} reference={np.round(r, 4)} '
                  f'R-B gap probe={g[0] - g[2]:+.4f} reference={r[0] - r[2]:+.4f}')
        return
    if mode == 'f2':
        a = np.fromfile(a_path, dtype='<f2').astype(np.float32)
        b = np.fromfile(b_path, dtype='<f2').astype(np.float32)
        n = min(a.size, b.size)
        d = np.abs(a[:n] - b[:n])
        print(f'{a_path} vs {b_path} (RGBA16F)')
        print(f'  samples={n} meanAbs={d.mean():.3e} max={d.max():.3e} '
              f'frac_equal={(d == 0).mean():.6f} frac_le_1step={(d <= 0.0009765625).mean():.6f}')
        return
    if mode == 'u2':
        a = np.fromfile(a_path, dtype='<u2').astype(np.int32)
        b = np.fromfile(b_path, dtype='<u2').astype(np.int32)
        n = min(a.size, b.size)
        d = np.abs(a[:n] - b[:n])
        print(f'{a_path} vs {b_path} (16-bit counts)')
        print(f'  samples={n} meanAbs={d.mean():.4f} max={d.max()} frac_equal={(d == 0).mean():.6f}')
        return
    raise SystemExit(f'unknown mode {mode}')


main()
