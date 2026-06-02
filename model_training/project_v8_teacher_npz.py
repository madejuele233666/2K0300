import argparse
import json
from pathlib import Path

import numpy as np


def parse_ints(text: str) -> list[int]:
    return [int(item.strip()) for item in text.split(",") if item.strip()]


def l2_normalize(values: np.ndarray) -> np.ndarray:
    norm = np.linalg.norm(values, axis=1, keepdims=True)
    return values / np.maximum(norm, 1.0e-8)


def fit_pca(values: np.ndarray, dim: int) -> tuple[np.ndarray, np.ndarray]:
    mean = np.mean(values, axis=0)
    centered = values - mean
    _u, _s, vt = np.linalg.svd(centered, full_matrices=False)
    return mean, vt[:dim].T


def apply_pca(values: np.ndarray, mean: np.ndarray, components: np.ndarray) -> np.ndarray:
    return (values - mean) @ components


def project_payload(source: Path, output_dir: Path, dim: int, normalize: bool) -> Path:
    with np.load(source, allow_pickle=True) as data:
        payload = {key: data[key] for key in data.files}
    embeddings = np.asarray(payload["embedding_float"], dtype=np.float64)
    prototypes = np.asarray(payload["prototypes"], dtype=np.float64)
    if embeddings.ndim != 2 or prototypes.ndim != 2:
        raise ValueError("source must contain 2D embedding_float and prototypes arrays")
    if dim >= embeddings.shape[1]:
        raise ValueError(f"projection dim must be smaller than source dim {embeddings.shape[1]}, got {dim}")

    mean, components = fit_pca(embeddings, dim)
    projected_embeddings = apply_pca(embeddings, mean, components)
    projected_prototypes = apply_pca(prototypes, mean, components)
    if normalize:
        projected_embeddings = l2_normalize(projected_embeddings)
        projected_prototypes = l2_normalize(projected_prototypes)

    out_payload = dict(payload)
    out_payload["embedding_float"] = projected_embeddings.astype(np.float32)
    out_payload["prototypes"] = projected_prototypes.astype(np.float32)
    out_payload["teacher_projection_kind"] = np.asarray("pca_l2" if normalize else "pca_raw")
    out_payload["teacher_projection_source"] = np.asarray(str(source))
    out_payload["teacher_projection_dim"] = np.asarray(dim, dtype=np.int64)
    out_payload["teacher_projection_mean"] = mean.astype(np.float32)
    out_payload["teacher_projection_components"] = components.astype(np.float32)
    out_payload.pop("embedding_int8", None)
    out_payload.pop("prototypes_int8", None)
    out_payload.pop("pred", None)
    out_payload.pop("int8_pred", None)
    out_payload.pop("margin", None)
    out_payload.pop("int8_margin", None)

    output_dir.mkdir(parents=True, exist_ok=True)
    out_path = output_dir / f"{source.stem}_pca{dim}{'_l2' if normalize else ''}.npz"
    np.savez_compressed(out_path, **out_payload)
    return out_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Project a V8 d24 compiled teacher NPZ to smaller embedding dimensions.")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--dims", default="12,16")
    parser.add_argument("--no-l2-normalize", action="store_false", dest="normalize")
    args = parser.parse_args()

    rows = []
    for dim in parse_ints(args.dims):
        path = project_payload(args.source, args.output_dir, dim, args.normalize)
        rows.append({"dim": dim, "path": str(path), "normalize": bool(args.normalize)})
    print(json.dumps({"projected": rows}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
