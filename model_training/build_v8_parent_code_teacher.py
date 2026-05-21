import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_offsets(value: str) -> np.ndarray:
    parts = [int(item) for item in value.replace(";", ",").split(",") if item]
    if len(parts) != 3:
        raise ValueError(f"expected three parent offsets, got {value!r}")
    return np.asarray(parts, dtype=np.int8)


def append_parent_code(
    values: np.ndarray,
    parents: np.ndarray,
    offsets: np.ndarray,
) -> np.ndarray:
    parent_code = offsets[parents.astype(np.int64)].reshape(-1, 1).astype(np.int8)
    return np.concatenate([values.astype(np.int8), parent_code], axis=1).astype(np.int8)


def build_teacher(
    *,
    source_npz: Path,
    output_dir: Path,
    parent_offsets: np.ndarray,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    with np.load(source_npz, allow_pickle=True) as data:
        payload = {key: data[key] for key in data.files}
    required = ["embedding_int8", "prototypes_int8", "parent", "prototype_parent"]
    missing = [key for key in required if key not in payload]
    if missing:
        raise ValueError(f"{source_npz} is missing required arrays: {missing}")

    embedding_int8 = append_parent_code(
        np.asarray(payload["embedding_int8"], dtype=np.int8),
        np.asarray(payload["parent"], dtype=np.int64),
        parent_offsets,
    )
    prototypes_int8 = append_parent_code(
        np.asarray(payload["prototypes_int8"], dtype=np.int8),
        np.asarray(payload["prototype_parent"], dtype=np.int64),
        parent_offsets,
    )
    out_payload: dict[str, np.ndarray] = {
        key: np.asarray(value)
        for key, value in payload.items()
        if key not in {"embedding_int8", "embedding_float", "prototypes_int8", "prototypes"}
    }
    out_payload["embedding_int8"] = embedding_int8
    out_payload["embedding_float"] = embedding_int8.astype(np.float32)
    out_payload["prototypes_int8"] = prototypes_int8
    out_payload["prototypes"] = prototypes_int8.astype(np.float32)
    out_payload["parent_code_offsets"] = parent_offsets.astype(np.int8)
    out_payload["qanchor_weight"] = np.ones(len(embedding_int8), dtype=np.float32)
    out_payload["source_params_npz"] = np.asarray(str(source_npz))
    out_path = output_dir / "parent_code_teacher.npz"
    np.savez_compressed(out_path, **out_payload)

    write_json(
        output_dir / "summary.json",
        {
            "source_params_npz": str(source_npz),
            "output_npz": str(out_path),
            "parent_offsets": [int(item) for item in parent_offsets.tolist()],
            "embedding_count": int(len(embedding_int8)),
            "prototype_count": int(len(prototypes_int8)),
            "source_dim": int(np.asarray(payload["embedding_int8"]).shape[1]),
            "teacher_dim": int(embedding_int8.shape[1]),
            "high_pressure_usage": "none",
        },
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Append a supervised parent-coded int8 dimension to normal V8 params.")
    parser.add_argument("--source-npz", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--parent-offsets", default="-64,0,64")
    args = parser.parse_args()
    build_teacher(
        source_npz=args.source_npz,
        output_dir=args.output_dir,
        parent_offsets=parse_offsets(args.parent_offsets),
    )


if __name__ == "__main__":
    main()
