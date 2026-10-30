"""Reproduce the Windows comparison inventory; leave Phase 3 acceptance open."""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
JSON_PATH = pathlib.Path('benchmarks/reports/phase3-comparison.json')
DOC_PATH = pathlib.Path('docs/PHASE3_COMPARISON_REPORT.md')
CAMPAIGNS = (
    ('P3-CMP-07', 'three-sdk-upload', ('odd', 'upload1080', 'upload4k', 'upload4k16f'), ('total_ms',)),
    ('P3-CMP-08', 'three-sdk-draw', ('draw1', 'draw1000', 'instanced1000'), ('record_ms', 'submit_ms', 'wait_ms', 'total_ms')),
    ('P3-CMP-09', 'three-sdk-pso', ('pso1', 'pso8'), ('create_ms', 'public_release_ms')),
    ('P3-CMP-10', 'three-sdk-allocation', ('buffer1', 'buffer1000', 'texture1', 'texture1000'), ('create_ms', 'public_release_ms')),
    ('P3-BEN-09', 'draw-gpu', ('draw1', 'draw1000', 'instanced1000'), ('cpu_total_ms', 'gpu_ms')),
)
LABELS = {'graphicssdk': 'GraphicsSDK', 'sdl': 'SDL pinned', 'llgl': 'LLGL + transfer-src patch v1'}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def record(root, path):
    return {'path': path.relative_to(root).as_posix(),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def statistics(s):
    require(type(s['processes']) is int and s['processes'] == 5
            and type(s['samples']) is int and s['samples'] == 150
            and type(s['samples_removed']) is int and s['samples_removed'] == 0,
            'Missing independent processes, retained samples, or samples removed')
    for key in ('min_ms', 'p50_ms', 'p95_ms', 'p99_ms', 'max_ms', 'MAD_ms'):
        require(type(s[key]) in (int, float) and math.isfinite(s[key]) and s[key] >= 0,
                'Invalid millisecond statistic')
    require(s['min_ms'] <= s['p50_ms'] <= s['p95_ms'] <= s['p99_ms'] <= s['max_ms'],
            'Percentiles out of order')
    require(len(s['process_medians_ms']) == 5 and all(
        type(x) in (int, float) and math.isfinite(x) and s['min_ms'] <= x <= s['max_ms']
        for x in s['process_medians_ms']), 'Invalid process medians')
    return s


def compose(receipts, summaries, roadmap, bindings):
    """Build only from replayed receipts, with explicit workload/clock/mode keys."""
    require(len(receipts) == len(summaries) == len(bindings) == len(CAMPAIGNS), 'Missing campaign')
    tasks = {t['id']: t for t in roadmap['tasks']}
    require(len(tasks) == len(roadmap['tasks']), 'Duplicate roadmap ID')
    campaigns = []
    identity = receipts[0]['identity']
    fingerprint = receipts[0]['SDK_source_fingerprint']
    sdl = receipts[0]['SDL_commit']
    for (task, name, scenarios, columns), r, summary, evidence in zip(CAMPAIGNS, receipts, summaries, bindings):
        require(task in tasks and tasks[task]['status'] == 'done' and r['task'] == summary['task'] == task,
                'Campaign is not an accepted roadmap task')
        require(r['identity'] == identity and r['SDK_source_fingerprint'] == fingerprint
                and r['SDL_commit'] == sdl, 'Cross-campaign hardware, driver, environment or source differs')
        require(r['inputs_unchanged'] is True and r['historical_timings_reused'] is False
                and r['overall_SDK_ranking'] is False, 'Campaign scope or frozen inputs changed')
        require(summary['historical_timings_reused'] is False and summary['overall_SDK_ranking'] is False,
                'Summary promotes historical timings or overall ranking')
        for key in ('task', 'source_commit', 'identity', 'SDK_source_fingerprint', 'SDL_commit', 'groups',
                    'verified_images', 'verified_bytes'):
            require(summary[key] == r[key], 'Summary differs from replayed native receipt: ' + key)
        require(summary['Linux_hardware_accepted'] is False, 'Windows evidence promoted to Linux')
        gpu = task == 'P3-BEN-09'
        require(r['GPU_timestamp_comparison_accepted' if gpu else 'performance_accepted'] is True,
                'Campaign not accepted')
        sdks = ('graphicssdk', 'sdl') if gpu else tuple(LABELS)
        expected = {(scenario, sdk, mode, column)
                    for scenario in scenarios for sdk in sdks
                    for mode in (('baseline', 'audit', 'timestamp') if gpu else ('uninstrumented',))
                    for column in columns if not gpu or column != 'gpu_ms' or mode == 'timestamp'}
        metrics = []
        seen = set()
        for group in r['groups']:
            mode = group['mode'] if gpu else 'uninstrumented'
            values = group['statistics'] if gpu else {group['column']: group['statistics']}
            for column, value in values.items():
                key = (group['scenario'], group['sdk'], mode, column)
                require(key in expected and key not in seen, 'Missing, duplicate or foreign metric dimension')
                seen.add(key)
                metrics.append(dict(scenario=key[0], sdk=key[1], mode=mode, column=column,
                                    clock='GPU' if column == 'gpu_ms' else 'CPU', unit='ms',
                                    statistics=statistics(value)))
        require(seen == expected, 'Incomplete scenario/SDK/column/mode coverage')
        counts = {key: r[key] for key in (
            ('measurement_GPU_samples', 'measurement_CPU_samples') if gpu else ('measurement_samples',))}
        for key, value in counts.items():
            require(type(value) is int and value > 0 and summary[key] == value, 'Sample count differs')
        if gpu:
            require(counts == dict(measurement_GPU_samples=len(scenarios)*len(sdks)*150,
                                   measurement_CPU_samples=len(scenarios)*len(sdks)*3*150),
                    'GPU/CPU impact sample accounting differs')
        else:
            require(counts['measurement_samples'] == len(scenarios)*len(sdks)*150,
                    'CPU samples counted more than once across columns')
        campaigns.append(dict(task=task, name=name, source_commit=r['source_commit'], evidence=evidence,
                              counts=counts, processes=len(r['runs']),
                              readback={k: r[k] for k in ('verified_images', 'verified_buffers', 'verified_bytes') if k in r},
                              metrics=metrics,
                              identity_attribution='actual_render_device_queue' if gpu else
                              'SDK_SDL_unique_name_native_DXGI; LLGL_actual_native_handles',
                              LLGL_profile=None if gpu else 'llgl-fixed-pin-transfer-src-v1'))
    # Include transitive prerequisites, so unavailable physical qualification is visible.
    gate = tasks['P3-GATE-02']
    required = set()
    def visit(task):
        require(task in tasks, 'Missing roadmap prerequisite: ' + task)
        if task in required:
            return
        required.add(task)
        for parent in tasks[task]['depends_on']:
            visit(parent)
    for task in gate['depends_on']:
        visit(task)
    pending = [dict(id=t['id'], title=t['title'], status=t['status'],
                    requires=t.get('requires', ''), depends_on=t['depends_on'])
               for t in roadmap['tasks'] if t['id'] in required and t['status'] != 'done']
    require(gate['status'] == 'todo' and pending, 'Partial Windows inventory cannot complete final gate')
    return dict(schema_version=1, task='P3-GATE-02', status='partial_windows_inventory',
                Phase3_completed=False, final_comparison_accepted=False, Linux_hardware_accepted=False,
                new_measurements_collected=False, overall_SDK_ranking=False,
                identity=identity, SDK_source_fingerprint=fingerprint, SDL_commit=sdl,
                campaigns=campaigns, pending_prerequisites=pending)


def collect(root=ROOT):
    receipts, summaries, bindings = [], [], []
    for _, name, _, _ in CAMPAIGNS:
        folder = root / ('benchmarks/reports/validation/2026-10-29-phase3-' + name)
        # Only the repository's reviewed standalone verifier executes. ZIP code and
        # saved executables are never loaded; the verifier replays raw data offline.
        spec = importlib.util.spec_from_file_location('report_verify_' + name.replace('-', '_'), folder/'verify.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with contextlib.redirect_stdout(io.StringIO()):
            receipt = module.verify(folder)
        receipts.append(receipt)
        summaries.append(read(folder/'summary.json'))
        bindings.append([record(root, folder/p) for p in (
            'native/receipt.json', 'summary.json', 'metrics.csv', 'source.zip', 'manifest.json', 'verify.py')])
    return compose(receipts, summaries, read(root/'docs/ROADMAP_TASKS.json'), bindings)


def render(report):
    lines = ['# Phase 3 비교 결과 통합 — Windows 완료 범위', '',
             '이 문서는 `tools/phase3_comparison_report.py`가 원본을 재검증한 뒤 생성한다. '
             '**P3-GATE-02는 미완료**다. 기존 측정의 통합이며 새 GPU 실행·새 표본 수집이 아니다.', '',
             '## 장치와 해석 범위', '',
             f"장치: {report['identity']['device_name']}. device UUID `{report['identity']['device_uuid']}`, "
             f"driver UUID `{report['identity']['driver_uuid']}`, native driver `{report['identity']['native_driver_version']}`.", '',
             f"제품 SDK fingerprint `{report['SDK_source_fingerprint']}`, SDL pin `{report['SDL_commit']}`.", '',
             '- LLGL은 사용자 승인 `llgl-fixed-pin-transfer-src-v1`이다. 고정 upstream 원본과 구분한다.',
             '- CPU 캠페인의 SDK/SDL은 유일한 이름·native Vulkan/DXGI 대조다. LLGL과 별도 GPU 캠페인만 실제 renderer native handle을 관측했다.',
             '- SDK/SDL application API 요청은 1.1, LLGL 별도 관측은 1.4.357이다. 같은 API 요청·내부 barrier·clear·staging 정책이라는 주장은 하지 않는다.',
             '- 업로드/CPU draw는 fence 완료까지의 wall time이다. PSO/자원 생성은 공개 create/release 호출이며 물리 메모리 회수·native 할당 수·cache 초기화를 입증하지 않는다.',
             '- GPU 구간은 render pass 직전 TOP_OF_PIPE부터 직후 BOTTOM_OF_PIPE까지다. query marker·의존성·스케줄링이 포함되며 비계측 GPU 시간으로 보정하지 않는다.',
             '- 캠페인 간 실행 시점·계측 모드가 다르다. 서로 다른 캠페인의 CPU/GPU P50을 빼거나 나누어 overhead·병목을 추정하지 않는다.',
             '- 각 행은 독립 프로세스 5개 × retained 30개다. column별 표본 수는 중복된 구간이므로 합산하지 않는다. P95/P99는 기술 통계이며 신뢰구간이 아니다.',
             '- 온도·전력·driver cache·다른 프로세스 활동을 배타적으로 통제하지 않았다. SDK 전체 순위나 차이의 원인은 확정하지 않는다.', '',
             '## 재검사와 라이선스 입력', '',
             '```text', 'python tools/phase3_comparison_report.py --check',
             'python tools/phase3_comparison_report.py --write', '```', '',
             '기본 동작도 check다. 각 캠페인의 독립 verifier로 raw 표본·참조·환경·source ZIP·'
             '라이선스 입력을 먼저 재검사한다. JSON에는 원본 receipt·요약·CSV·ZIP·manifest·verifier의 '
             'SHA-256을 기록한다. 각 ZIP의 고지·compiler 입력과 원본 보고서의 재실행 명령을 함께 보존한다. '
             '외부 실행 파일을 재배포하지 않으며 hash는 외부 신뢰 서명이 아니다. '
             '이 검사는 offline 재생이며 실제 GPU 재실행을 대신하지 않는다.', '']
    for c in report['campaigns']:
        path = c['evidence'][0]['path'].rsplit('/native/', 1)[0]
        lines += [f"## {c['task']} — {c['name']}", '',
                  f"실행 기반 commit `{c['source_commit']}`. [원본 receipt](../{path}/native/receipt.json), "
                  f"[source·license ZIP](../{path}/source.zip), [manifest](../{path}/manifest.json), "
                  f"[독립 verifier](../{path}/verify.py).", '',
                  f"전체 프로세스 {c['processes']}개. 계수: " + ', '.join(f'{k}={v}' for k, v in c['counts'].items()) + '.', '',
                  '전체 참조 검사: ' + ', '.join(f'{k}={v}' for k, v in c['readback'].items()) + '.', '',
                  '| 입력 | SDK | 모드 | clock / 구간 | P50 ms | P95 ms | P99 ms | MAD ms |',
                  '|---|---|---|---|---:|---:|---:|---:|']
        for m in c['metrics']:
            s = m['statistics']
            lines.append(f"| {m['scenario']} | {LABELS[m['sdk']]} | {m['mode']} | {m['clock']} / {m['column']} | "
                         + ' | '.join(f'{s[k]:.6f}' for k in ('p50_ms', 'p95_ms', 'p99_ms', 'MAD_ms')) + ' |')
        lines += ['']
    lines += ['## 최종 완료에 필요한 잔여 항목', '',
              '아래 상태는 현재 로드맵에서 가져온다. 과거 보고서의 당시 `next_task`·pending 목록은 '
              '수정하지 않는다. Linux software 준비 성공은 물리 GPU 성능·표시 수용을 대신하지 않는다.', '',
              '| ID | 작업 | 상태 | 필요한 환경 |', '|---|---|---|---|']
    for t in report['pending_prerequisites']:
        lines.append(f"| {t['id']} | {t['title']} | {t['status']} | {t['requires'].replace('|', '/')} |")
    lines += ['', 'P3-GATE-01 통합 회귀·실제 Linux 표시 수용·P3-GATE-03 최종 판정도 별도 미완료다. '
              '이 통합 도구는 작업 상태를 완료로 변경하지 않는다.', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--write', action='store_true')
    mode.add_argument('--check', action='store_true')
    args = parser.parse_args()
    report = collect()
    outputs = {JSON_PATH: json.dumps(report, ensure_ascii=False, indent=2) + '\n', DOC_PATH: render(report)}
    for path, content in outputs.items():
        if args.write:
            (ROOT/path).write_bytes(content.encode('utf-8'))
        else:
            require((ROOT/path).read_text(encoding='utf-8') == content, str(path) + ' is stale; run --write')
    print('PASS 5 replayed Windows campaigns; CPU/GPU/modes separate; final/Linux gates remain pending')


if __name__ == '__main__':
    main()
