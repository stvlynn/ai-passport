from pathlib import Path
from huggingface_hub import snapshot_download
root = Path(__file__).resolve().parent
snapshot_download(repo_id="mlx-community/whisper-base-mlx", revision="1e3e249fb8d01c655324bd6841b1deadffd6d04c", local_dir=root / "models" / "whisper-base-mlx", allow_patterns=["config.json", "weights.npz", "weights.safetensors", "model.safetensors", "README.md"])
