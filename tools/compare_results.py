"""Compare compatible summary files; differences are observations, never an SDK ranking."""
import argparse
import csv
import json
import pathlib
from run_benchmarks import comparable, regression

def compare(baselines, candidates, baseline_sdk, candidate_sdk):
    rows = []
    for old in baselines:
        if old["sdk"] != baseline_sdk:
            continue
        for new in candidates:
            if new["sdk"] != candidate_sdk or old["scenario"] != new["scenario"] or old["count"] != new["count"]:
                continue
            if not comparable(old, new):
                raise ValueError("Cannot compare different environments, workloads or measurement contracts")
            for metric in sorted(old["metrics"].keys() & new["metrics"].keys()):
                a, b = old["metrics"][metric], new["metrics"][metric]
                if a is None or b is None:
                    continue
                outcome = regression(a, b)
                rows.append({"scenario": old["scenario"], "count": old["count"], "metric": metric,
                             "baseline_sdk": baseline_sdk, "candidate_sdk": candidate_sdk,
                             "baseline_p50_ms": a["p50"], "candidate_p50_ms": b["p50"],
                             "baseline_p95_ms": a["p95"], "candidate_p95_ms": b["p95"],
                             "baseline_p99_ms": a["p99"], "candidate_p99_ms": b["p99"],
                             "cost_change_percent": outcome.get("cost_change_percent"),
                             "advisory": outcome["status"] if baseline_sdk == candidate_sdk else "cross_sdk_observation_only"})
    if not rows:
        raise ValueError("No comparable measurements")
    return rows

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline", type=pathlib.Path)
    p.add_argument("candidate", type=pathlib.Path)
    p.add_argument("--baseline-sdk", default="graphicssdk")
    p.add_argument("--candidate-sdk", default="graphicssdk")
    p.add_argument("--output", type=pathlib.Path, required=True)
    args = p.parse_args()
    rows = compare(json.loads(args.baseline.read_text()), json.loads(args.candidate.read_text()), args.baseline_sdk, args.candidate_sdk)
    if args.output.exists():
        p.error("Refusing to overwrite a comparison")
    args.output.mkdir(parents=True)
    with (args.output / "comparison.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    lines = ["# Measurement comparison", "", f"Baseline: `{args.baseline}` ({args.baseline_sdk}). Candidate: `{args.candidate}` ({args.candidate_sdk}).", "",
             "Cost delta = 100 * (candidate P50 / baseline P50 - 1). Negative is lower measured cost.",
             "Advisory warnings use repeated run medians and MAD; they do not establish statistical significance.", "",
             "| Scenario | Count | Metric | Baseline P50 ms | Candidate P50 ms | Change % | Advisory |",
             "|---|---:|---|---:|---:|---:|---|"]
    for row in rows:
        delta = row['cost_change_percent']
        lines.append(f"| {row['scenario']} | {row['count']} | {row['metric']} | {row['baseline_p50_ms']:.6f} | {row['candidate_p50_ms']:.6f} | {delta:.2f} | {row['advisory']} |")
    (args.output / "comparison.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

if __name__ == "__main__":
    main()
