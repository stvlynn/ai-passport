#!/usr/bin/env python3
"""Local-only 16 kHz PCM WAV -> UTF-8 text for AI Passport Connector."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys
import tempfile
import wave

ROOT = Path(__file__).resolve().parent
# The model is installed ahead of time; transcription never contacts a model host.
os.environ.setdefault("HF_HUB_OFFLINE", "1")
os.environ.setdefault("HF_HUB_DISABLE_TELEMETRY", "1")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--model", type=Path, default=ROOT / "models" / "whisper-base-mlx")
    parser.add_argument("--language", default="zh")
    args = parser.parse_args()
    import numpy as np

    with wave.open(str(args.input), "rb") as wav:
        if (wav.getnchannels(), wav.getframerate(), wav.getsampwidth(), wav.getcomptype()) != (1, 16000, 2, "NONE"):
            raise ValueError("Input must be uncompressed 16 kHz mono PCM16 WAV")
        if not 0 < wav.getnframes() <= 16000 * 30:
            raise ValueError("Input must contain between 0 and 30 seconds of audio")
        frames = wav.readframes(wav.getnframes())
        if len(frames) != wav.getnframes() * 2:
            raise ValueError("Truncated WAV audio")
    audio = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
    if not (args.model / "config.json").is_file():
        raise FileNotFoundError("Local ASR model is not installed")
    # Exact digital silence should never become hallucinated words.
    if np.max(np.abs(audio)) == 0:
        transcript = ""
    else:
        import mlx_whisper
        result = mlx_whisper.transcribe(
            audio,
            path_or_hf_repo=str(args.model),
            language=args.language,
            task="transcribe",
            temperature=0.0,
            condition_on_previous_text=False,
            verbose=None,
        )
        transcript = str(result["text"]).strip()
    output = args.output.resolve()
    fd, temp_name = tempfile.mkstemp(prefix=".asr-", suffix=".txt", dir=output.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(transcript)
            if transcript:
                stream.write("\n")
        os.replace(temp_name, output)
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        # Never echo input filenames or transcripts into logs.
        print(f"ASR failed: {type(error).__name__}", file=sys.stderr)
        raise SystemExit(1)
