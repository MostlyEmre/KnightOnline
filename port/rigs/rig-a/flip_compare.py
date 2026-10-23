#!/usr/bin/env python3
"""Image-parity check: NVIDIA FLIP between reference (Windows/WARP) and test (macOS/Metal)
PNGs with the same file names. Writes a magma FLIP map and a side-by-side strip per image
and a markdown table: mean FLIP, 99th percentile, fraction of pixels above 0.1, worst pixel.

usage: flip_compare.py <reference dir> <test dir> <out dir> [--threshold 0.05]
needs: pip install flip-evaluator numpy pillow
"""

import argparse
import os

import flip_evaluator as flip
import numpy as np
from PIL import Image


def load(path):
	return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("ref")
	ap.add_argument("test")
	ap.add_argument("out")
	ap.add_argument("--threshold", type=float, default=0.05, help="max allowed mean FLIP per image")
	ap.add_argument("--ppd", type=float, default=67.0, help="pixels per degree (FLIP default 67: 0.7 m from a 24in 4K panel)")
	a = ap.parse_args()
	os.makedirs(a.out, exist_ok=True)

	rows, failed = [], 0
	for name in sorted(os.listdir(a.ref)):
		if not name.lower().endswith(".png"):
			continue
		tpath = os.path.join(a.test, name)
		if not os.path.exists(tpath):
			rows.append((name, None))
			failed += 1
			continue
		ref, test = load(os.path.join(a.ref, name)), load(tpath)
		if ref.shape != test.shape:
			rows.append((name, f"size mismatch {ref.shape} vs {test.shape}"))
			failed += 1
			continue
		emap, mean, _ = flip.evaluate(ref, test, "LDR", applyMagma=False, parameters={"ppd": a.ppd})
		emap = np.squeeze(emap)
		magma, _, _ = flip.evaluate(ref, test, "LDR", applyMagma=True, computeMeanError=False, parameters={"ppd": a.ppd})
		strip = np.concatenate([ref, test, np.squeeze(magma)[..., :3]], axis=1)
		Image.fromarray((np.clip(strip, 0, 1) * 255).astype(np.uint8)).save(os.path.join(a.out, name.replace(".png", "-ref-test-flip.png")))
		stats = {
			"mean": float(mean),
			"p99": float(np.percentile(emap, 99)),
			"over01": float((emap > 0.1).mean()),
			"max": float(emap.max()),
			"exact": float((np.abs(ref - test).max(axis=2) == 0).mean()),
		}
		if stats["mean"] > a.threshold:
			failed += 1
		rows.append((name, stats))

	lines = [f"| Image | Mean FLIP | p99 | Pixels > 0.1 | Max | Bit-exact pixels | ≤ {a.threshold} |", "|---|---:|---:|---:|---:|---:|---|"]
	for name, st in rows:
		if st is None:
			lines.append(f"| {name} | missing in test | | | | | ✗ |")
		elif isinstance(st, str):
			lines.append(f"| {name} | {st} | | | | | ✗ |")
		else:
			ok = "✓" if st["mean"] <= a.threshold else "✗"
			lines.append(f"| {name} | {st['mean']:.4f} | {st['p99']:.3f} | {st['over01'] * 100:.2f}% | {st['max']:.3f} | {st['exact'] * 100:.1f}% | {ok} |")
	report = "\n".join(lines) + "\n"
	open(os.path.join(a.out, "flip-report.md"), "w").write(report)
	print(report)
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
