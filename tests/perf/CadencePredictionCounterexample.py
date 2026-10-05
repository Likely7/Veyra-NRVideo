"""Adversarial proof for plan 1c, independent of GPU/model performance.

Even a perfect learned cadence cannot identify a transient change on a frame
whose pixels it never inspects. An identity enhancer makes lost source content
observable without attributing this simulation to the product or NR runtime.
"""
from pathlib import Path
import hashlib
import json
import sys


def frame(content_id):
    # Authored 16x16 RGBA image, including an actual changed source pixel.
    return bytes((content_id % 251, 37, 89, 255)) * 256


def cadence_frames(cadence, count=160):
    sequence = []
    content_id = -1
    for index in range(count):
        if cadence[index % len(cadence)]:
            content_id += 1
        sequence.append(frame(content_id))
    return sequence


def plan_prediction(sequence, cadence, start=32, verify_every=8):
    """Optimistic candidate: already knows the exact transport cadence.

    Inspect all new slots and every eighth slot; otherwise reuse blindly.
    The verification detects an unexpected different pixel on a predicted
    duplicate *only when that slot was inspected*. The identity enhancer
    processes all accepted frames exactly, so it introduces no image noise.
    """
    output, last, missed, detected = [], None, [], []
    for index, pixels in enumerate(sequence):
        expected_new = cadence[index % len(cadence)]
        inspected = index < start or expected_new or index % verify_every == 0
        if inspected:
            if not expected_new and pixels != last and index >= start:
                detected.append(index)
            last = pixels
        elif pixels != last:
            missed.append(index)
        output.append(last)
    return output, missed, detected


def case(name, cadence):
    pristine = cadence_frames(cadence)
    normal, missed, detected = plan_prediction(pristine, cadence)
    assert normal == pristine and not missed and not detected
    attack = next(i for i in range(49, 90)
                  if not cadence[i % len(cadence)] and i % 8 != 0)
    altered = pristine.copy()
    changed = bytearray(altered[attack])
    changed[(7 * 16 + 9) * 4] ^= 0x80
    altered[attack] = bytes(changed)
    candidate, missed, detected = plan_prediction(altered, cadence)
    unequal = [i for i, (a, b) in enumerate(zip(candidate, altered)) if a != b]
    assert attack in missed and attack in unequal and attack not in detected
    # Exact comparison accepts this distinct frame; no source pixels vanish.
    exact, last, reused = [], None, 0
    for pixels in altered:
        if pixels == last:
            reused += 1
        else:
            last = pixels
        exact.append(last)
    assert exact == altered
    return {'case': name, 'cadence': list(cadence), 'frameCount': len(altered),
            'singlePixelTransientFrame': attack, 'candidateDifferentFrames': unequal,
            'uninspectedChangedFrames': missed, 'detectedPredictionErrors': detected,
            'recoveryWithinOneFrame': False,
            'exactComparisonDifferentFrames': 0, 'exactReuseFrames': reused,
            'sourceSha256': hashlib.sha256(b''.join(altered)).hexdigest()}


if __name__ == '__main__':
    path = Path(sys.argv[1])
    path.parent.mkdir(parents=True, exist_ok=True)
    results = [case('30-in-60', (True, False)),
               case('24-in-60 whole-frame 2:3 repeat', (True, False, True, False, False)),
               case('40-in-120', (True, False, False))]
    receipt = {'purpose': 'Correctness counterexample; no real-player/GPU performance claim',
               'candidateRule': 'Blind reuse of predicted duplicates; verify every 8 slots',
               'passed': True, 'rejectPlan1c': True, 'cases': results}
    with path.open('x', encoding='utf-8') as out:
        json.dump(receipt, out, ensure_ascii=False, indent=2)
    print(json.dumps(receipt, ensure_ascii=False))
