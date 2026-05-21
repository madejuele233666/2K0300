import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames: list[str] = []
    for row in rows:
        for key in row:
            if key not in fieldnames:
                fieldnames.append(key)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def parse_source(text: str) -> tuple[str, Path]:
    name, path = text.split("=", 1)
    return name.strip(), Path(path.strip())


def event_key(row: dict[str, str]) -> tuple[str, int, str, int]:
    return (
        str(row["group"]),
        int(row["base_query_index"]),
        str(row["perturb"]),
        int(row["event_index"]),
    )


def load_events(path: Path) -> dict[tuple[str, int, str, int], dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return {event_key(row): row for row in csv.DictReader(handle)}


def wrong(row: dict[str, str]) -> bool:
    return str(row["wrong"]).lower() == "true" or int(row["stress_pred"]) != int(row["parent"])


def summarize_wrong(rows: list[dict[str, str]], pred_by_key: dict[tuple[str, int, str, int], int] | None = None) -> dict[str, Any]:
    grouped: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    wrong_events = 0
    for row in rows:
        key = event_key(row)
        value_wrong = int(pred_by_key[key]) != int(row["parent"]) if pred_by_key is not None else wrong(row)
        grouped[str(row["group"])][0] += int(value_wrong)
        grouped[str(row["group"])][1] += 1
        wrong_events += int(value_wrong)
    return {
        "wrong_events": int(wrong_events),
        "total_events": int(len(rows)),
        "per_group": {
            group: {
                "wrong": int(values[0]),
                "total": int(values[1]),
                "wrong_rate": float(values[0] / max(values[1], 1)),
            }
            for group, values in sorted(grouped.items())
        },
    }


def source_rows(name: str, rows: list[dict[str, str]]) -> dict[str, Any]:
    summary = summarize_wrong(rows)
    per_group = summary["per_group"]
    return {
        "name": name,
        "wrong_events": summary["wrong_events"],
        "total_events": summary["total_events"],
        "low_wrong_rate": float(per_group.get("low", {}).get("wrong_rate", 0.0)),
        "control_wrong_rate": float(per_group.get("control", {}).get("wrong_rate", 0.0)),
    }


def oracle_any(
    *,
    base_rows: list[dict[str, str]],
    sources: dict[str, dict[tuple[str, int, str, int], dict[str, str]]],
    source_order: list[str],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    pred_by_key: dict[tuple[str, int, str, int], int] = {}
    trace: list[dict[str, Any]] = []
    for row in base_rows:
        key = event_key(row)
        chosen = source_order[0]
        chosen_pred = int(sources[chosen][key]["stress_pred"])
        for name in source_order:
            candidate = sources[name][key]
            if int(candidate["stress_pred"]) == int(row["parent"]):
                chosen = name
                chosen_pred = int(candidate["stress_pred"])
                break
        pred_by_key[key] = int(chosen_pred)
        if chosen != source_order[0]:
            trace.append(
                {
                    "group": row["group"],
                    "base_query_index": int(row["base_query_index"]),
                    "sample_index": int(row["sample_index"]),
                    "view_label": row["view_label"],
                    "perturb": row["perturb"],
                    "perturb_family": row["perturb_family"],
                    "parent": int(row["parent"]),
                    "base_source": source_order[0],
                    "chosen_source": chosen,
                    "base_wrong": wrong(sources[source_order[0]][key]),
                    "chosen_pred": int(chosen_pred),
                }
            )
    return summarize_wrong(base_rows, pred_by_key), trace


def best_by_field(
    *,
    base_rows: list[dict[str, str]],
    sources: dict[str, dict[tuple[str, int, str, int], dict[str, str]]],
    source_order: list[str],
    field: str,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    groups: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in base_rows:
        groups[str(row[field])].append(row)
    selected_by_value: dict[str, str] = {}
    rows_out: list[dict[str, Any]] = []
    for value, items in sorted(groups.items()):
        best_name = source_order[0]
        best_wrong = len(items) + 1
        per_source: dict[str, int] = {}
        for name in source_order:
            count = sum(int(wrong(sources[name][event_key(row)])) for row in items)
            per_source[name] = int(count)
            if count < best_wrong:
                best_wrong = int(count)
                best_name = name
        selected_by_value[value] = best_name
        rows_out.append(
            {
                "field": field,
                "value": value,
                "selected_source": best_name,
                "wrong": int(best_wrong),
                "total": int(len(items)),
                "wrong_rate": float(best_wrong / max(len(items), 1)),
                "per_source_wrong_json": json.dumps(per_source, ensure_ascii=False),
            }
        )
    pred_by_key: dict[tuple[str, int, str, int], int] = {}
    for row in base_rows:
        source_name = selected_by_value[str(row[field])]
        pred_by_key[event_key(row)] = int(sources[source_name][event_key(row)]["stress_pred"])
    return summarize_wrong(base_rows, pred_by_key), rows_out


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze high-pressure teacher-source oracle coverage for V8.")
    parser.add_argument("--source", action="append", required=True, help="name=stress_events.csv")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--best-by-fields", default="group,view_label,perturb_family,selection_margin_bucket")
    args = parser.parse_args()

    source_items = [parse_source(item) for item in args.source]
    source_names = [name for name, _path in source_items]
    sources = {name: load_events(path) for name, path in source_items}
    common_keys = set.intersection(*(set(rows) for rows in sources.values()))
    if not common_keys:
        raise ValueError("sources have no common events")
    dropped = {name: len(rows) - len(common_keys) for name, rows in sources.items()}
    base_name = source_names[0]
    base_rows = [sources[base_name][key] for key in sorted(common_keys)]

    source_summary = [source_rows(name, [sources[name][key] for key in sorted(common_keys)]) for name in source_names]
    oracle_summary, oracle_trace = oracle_any(base_rows=base_rows, sources=sources, source_order=source_names)
    write_csv(args.output_dir / "source_summary.csv", source_summary)
    write_csv(args.output_dir / "oracle_chosen_events.csv", oracle_trace)

    best_by_summaries: list[dict[str, Any]] = []
    for field in [item.strip() for item in args.best_by_fields.split(",") if item.strip()]:
        summary, rows = best_by_field(base_rows=base_rows, sources=sources, source_order=source_names, field=field)
        best_by_summaries.append(
            {
                "field": field,
                "wrong_events": int(summary["wrong_events"]),
                "total_events": int(summary["total_events"]),
                "low_wrong_rate": float(summary["per_group"].get("low", {}).get("wrong_rate", 0.0)),
                "control_wrong_rate": float(summary["per_group"].get("control", {}).get("wrong_rate", 0.0)),
            }
        )
        write_csv(args.output_dir / f"best_by_{field}.csv", rows)

    chosen_counts = Counter(row["chosen_source"] for row in oracle_trace)
    summary_json = {
        "sources": {name: str(path) for name, path in source_items},
        "source_order": source_names,
        "high_pressure_usage": "diagnostic_only",
        "common_events": int(len(common_keys)),
        "dropped_events": dropped,
        "source_summary": source_summary,
        "oracle_any": {
            "wrong_events": int(oracle_summary["wrong_events"]),
            "total_events": int(oracle_summary["total_events"]),
            "low_wrong_rate": float(oracle_summary["per_group"].get("low", {}).get("wrong_rate", 0.0)),
            "control_wrong_rate": float(oracle_summary["per_group"].get("control", {}).get("wrong_rate", 0.0)),
            "chosen_non_base_count": int(len(oracle_trace)),
            "chosen_counts": dict(chosen_counts),
        },
        "best_by": best_by_summaries,
    }
    write_json(args.output_dir / "summary.json", summary_json)
    print(json.dumps(summary_json, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
