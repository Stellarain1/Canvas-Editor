#!/usr/bin/env python3
"""Export a Beat This! checkpoint (CPJKU/beat_this, ISMIR 2024, MIT) to ONNX for Canvas Editor.

The graph is only the network: input "spect" is the log-mel spectrogram (1, frames, 128) and the
outputs "beat" / "downbeat" are per-frame logits (1, frames). The front-end (22.05 kHz mono,
STFT, mel, log1p) and the peak picking live in C++ (Canvas/Core/BeatDetect.cpp).
The time axis is dynamic; the editor feeds 1500-frame chunks like the reference code.

Setup (CPU torch is enough, nothing here needs a GPU):
	python -m venv venv
	venv/Scripts/pip install torch torchaudio --index-url https://download.pytorch.org/whl/cpu
	venv/Scripts/pip install tqdm einops soxr rotary-embedding-torch onnx onnxruntime onnxscript soundfile numpy
	venv/Scripts/pip install beat-this --no-deps

Run:
	python tools/export_beat_this_onnx.py --checkpoint small0 --out MikuMikuWorld/res/models/beat_this.onnx
	python tools/export_beat_this_onnx.py --checkpoint final0 --out beat_this_final.onnx
"""
import argparse
import os

import numpy as np
import torch

from beat_this.inference import load_model


class Wrapper(torch.nn.Module):
	"""Dict outputs do not export nicely, so return a plain tuple."""

	def __init__(self, model):
		super().__init__()
		self.model = model

	def forward(self, spect):
		out = self.model(spect)
		return out["beat"], out["downbeat"]


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--checkpoint", default="small0", help="final0..2 (78 MB), small0..2 (8 MB), or a .ckpt path")
	ap.add_argument("--out", required=True)
	ap.add_argument("--opset", type=int, default=17)
	args = ap.parse_args()

	model = load_model(args.checkpoint, "cpu").eval()
	wrapper = Wrapper(model).eval()
	dummy = torch.randn(1, 1500, 128)

	os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
	with torch.no_grad():
		torch.onnx.export(
			wrapper, (dummy,), args.out,
			input_names=["spect"], output_names=["beat", "downbeat"],
			dynamic_axes={"spect": {1: "frames"}, "beat": {1: "frames"}, "downbeat": {1: "frames"}},
			opset_version=args.opset, dynamo=False,
		)

	# check it against PyTorch: a full chunk and a short odd-sized one
	import onnxruntime as ort
	sess = ort.InferenceSession(args.out, providers=["CPUExecutionProvider"])
	worst = 0.0
	for frames in (1500, 777, 120):
		x = torch.randn(1, frames, 128) * 2.0
		with torch.no_grad():
			ref_b, ref_d = wrapper(x)
		got_b, got_d = sess.run(None, {"spect": x.numpy()})
		diff = max(np.abs(got_b - ref_b.numpy()).max(), np.abs(got_d - ref_d.numpy()).max())
		print(f"frames {frames}: max abs logit diff {diff:.2e}")
		worst = max(worst, float(diff))
	print(f"{args.out}: {os.path.getsize(args.out) / 1e6:.1f} MB, worst diff {worst:.2e}")
	if worst > 1e-3:
		raise SystemExit("ONNX output does not match PyTorch")


if __name__ == "__main__":
	main()
