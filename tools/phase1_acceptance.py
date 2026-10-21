"""Fail-closed Phase 1 completion gate. Sampled memory and missing adapters cannot pass."""
import argparse
import json
import pathlib

def valid_hash(value):
    return isinstance(value,str) and len(value)==64 and all(c in '0123456789abcdef' for c in value)


def evaluate(evidence):
    gates={
        'advanced_native_cpu_references':evidence.get('advanced_native_cpu_references') is True,
        'repeated_advanced_observations':evidence.get('repeated_advanced_observations') is True,
        'application_cache_cold_warm_hit_miss':evidence.get('application_cache_cold_warm_hit_miss') is True,
        'matching_advanced_sdk_comparisons':evidence.get('matching_advanced_sdk_comparisons') is True,
        'continuous_transient_gpu_memory':False,
    }
    memory=evidence.get('gpu_memory_trace',{})
    # A successful launch or an ETL filename alone does not establish peak coverage.
    gates['continuous_transient_gpu_memory']=(isinstance(memory,dict) and memory.get('status')=='verified'
        and memory.get('event_loss')==0 and type(memory.get('event_loss')) is int
        and memory.get('allocation_lifetime_reconciled') is True and memory.get('transient_positive_control') is True
        and valid_hash(memory.get('process_filtered_event_sha256')) and valid_hash(memory.get('raw_etl_sha256')))
    return {'phase1_complete':all(gates.values()),'gates':gates,'unmet':[k for k,v in gates.items() if not v]}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('evidence',type=pathlib.Path);args=p.parse_args()
    result=evaluate(json.loads(args.evidence.read_text(encoding='utf-8')))
    print(json.dumps(result,indent=2));return 0 if result['phase1_complete'] else 1


if __name__=='__main__':raise SystemExit(main())
