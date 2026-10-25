"""Inspect and render the granular roadmap. Read-only unless --write is supplied."""
import argparse
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DATA = ROOT / 'docs/ROADMAP_TASKS.json'
DOCUMENT = ROOT / 'docs/ROADMAP.md'
STATUS = {'done': '완료', 'todo': '미완료', 'deferred': '보류'}
GATE = {'required': '필수', 'conditional': '범위 결정 후', 'followup': '후속 최적화', 'deferred': '보류 유지'}


def require(value, message):
    if not value:
        raise ValueError(message)


def load():
    data = json.loads(DATA.read_text(encoding='utf-8'))
    require(type(data['schema_version']) is int and data['schema_version'] == 1, 'Unsupported schema')
    require(type(data['active_phase']) is int and data['active_phase'] in range(8), 'Unknown active phase')
    require(re.fullmatch(r'[0-9a-f]{40}', data['baseline_commit']), 'Invalid baseline commit')
    require(isinstance(data['tasks'], list) and isinstance(data['groups'], list), 'Task/group list required')
    tasks = data['tasks']
    ids = {t['id']: t for t in tasks}
    require(len(tasks) == len(ids), 'Duplicate task ID')
    groups = {g['id']: g for g in data['groups']}
    require(len(groups) == len(data['groups']), 'Duplicate group ID')
    require({p['id'] for p in data['phases']} == set(range(8)) and len(data['phases']) == 8, 'Phase index differs')
    for task in tasks:
        name = task['id']
        require(re.fullmatch(r'P[0-7]-[A-Z]+-\d{2}', name), 'Invalid ID: ' + name)
        require(task['status'] in STATUS and task['gate'] in GATE, 'Invalid status/gate: ' + name)
        require(task['group'] in groups and name.startswith(task['group'] + '-'), 'Invalid group: ' + name)
        require(type(task['phase']) is int and task['phase'] == int(name[1]) == groups[task['group']]['phase'], 'Phase differs: ' + name)
        require(all(isinstance(task[k], str) and task[k].strip() for k in ('title', 'work', 'done_when')), 'Missing work/acceptance: ' + name)
        require(isinstance(task['depends_on'], list) and isinstance(task['evidence'], list), 'Dependency/evidence list required: ' + name)
        require(len(set(task['depends_on'])) == len(task['depends_on']), 'Duplicate dependency: ' + name)
        require(all(dep in ids and dep != name for dep in task['depends_on']), 'Unknown/self dependency: ' + name)
        if task['status'] == 'done':
            require(task['evidence'], 'Completed task needs evidence: ' + name)
            require(all(ids[dep]['status'] == 'done' for dep in task['depends_on']), 'Completed task has unfinished dependency: ' + name)
        if task['status'] == 'deferred':
            require(task.get('reason') and task['gate'] == 'deferred', 'Missing deferral: ' + name)
        if task['gate'] == 'conditional':
            require(task.get('scope_from') in ids, 'Conditional task needs a scope decision: ' + name)
        for link in task['evidence']:
            path = ROOT / link
            require(not pathlib.Path(link).is_absolute() and path.resolve().is_relative_to(ROOT), 'External evidence path: ' + name)
            require(path.is_file(), 'Missing evidence: ' + link)
    visited, active = set(), set()

    def visit(name):
        require(name not in active, 'Dependency cycle: ' + name)
        if name in visited:
            return
        active.add(name)
        for dep in ids[name]['depends_on']:
            visit(dep)
        active.remove(name)
        visited.add(name)

    for name in ids:
        visit(name)
    require(len(set(data['next_order'])) == len(data['next_order']), 'Duplicate next-order ID')
    require(all(name in ids for name in data['next_order']), 'Unknown next-order ID')
    history = ROOT / data['history']['path']
    marker = b'<!-- preserved-roadmap-start -->\n'
    raw = history.read_bytes().replace(b'\r\n', b'\n')
    require(raw.count(marker) == 1, 'Historical roadmap marker differs')
    require(hashlib.sha256(raw.split(marker, 1)[1]).hexdigest() == data['history']['original_lf_sha256'], 'Historical roadmap text changed')
    return data, ids


def counts(tasks):
    return {status: sum(t['status'] == status for t in tasks) for status in STATUS}


def ready(task, ids):
    return (task['status'] == 'todo' and task['gate'] in ('required', 'followup')
            and all(ids[dep]['status'] == 'done' for dep in task['depends_on']))


def next_task(data, ids, phase):
    order = data['next_order'] + [t['id'] for t in data['tasks'] if t['id'] not in data['next_order']]
    return next((ids[name] for name in order if ids[name]['phase'] == phase and ready(ids[name], ids)), None)


def cell(value):
    return str(value).replace('|', '\\|').replace('\n', '<br>')


def render(data, ids):
    lines = [
        '# GraphicsSDK 세부 개발 로드맵', '',
        '기준일: ' + data['updated_at'] + ' · 기준 코드: `' + data['baseline_commit'][:7] + '` · 현재 단계: **Phase ' + str(data['active_phase']) + '**', '',
        '이 문서는 현재 작업 상태표다. 원본 요구사항의 Phase 순서와 기존 검증 범위를 유지하면서 큰 묶음을 독립 작업으로 분할했다. '
        '초기 개발 snapshot 0.0.1이며, Phase 3 또는 v0.1 전체 완료를 의미하지 않는다.', '',
        '상태 원본은 [ROADMAP_TASKS.json](ROADMAP_TASKS.json), 이전 누적 기록은 [' + pathlib.Path(data['history']['path']).name + '](' + pathlib.Path(data['history']['path']).name + ')이다. '
        '과거 보고서의 당시 미완료 문구는 현재 잔여 목록에 중복 합산하지 않는다.', '',
        '## 지시·체크 방법', '',
        '- **ID로 지시:** `P3-LNX-01 진행해줘`, `P3-WSI-03부터 진행해줘`처럼 한 작업을 지정한다.',
        '- **다음 개발:** 현재 Phase의 미완료 필수 작업(Phase 6에서는 후속 최적화) 중 선행 조건을 충족한 작업을 `next_order` 순서로 선택한다. 조건부 작업·보류 조사는 자동 선택하지 않는다.',
        '- **체크 시점:** 각 ID의 완료 기준과 필요한 실행 증거를 충족하면 그 ID만 완료로 바꾼다. 큰 그룹을 끝낼 때까지 체크를 미루지 않는다.',
        '- **부분 완료:** 구현만 끝나고 GPU 검증이 남았다면 검증 ID는 미완료다. 한 ID도 너무 크면 새 하위 작업 ID로 나누고 기존 ID를 재사용하지 않는다.',
        '- **근거:** 완료 항목에는 해당 플랫폼·fingerprint의 보고서/실행 증거를 연결한다. 코드가 있다는 이유, 다른 플랫폼의 성공, 기대한 미지원 응답만으로 실제 기능 지원을 완료 처리하지 않는다.',
        '- **코드 변경 후:** 관련 최종 회귀·현재 제품 측정·단계 수용 ID를 다시 검토한다. 과거 완료 증거는 보존하고 새 fingerprint 검증으로 재귀속하지 않는다.',
        '- **환경:** 선행 ID가 끝나도 Linux/Mac/GPU/사람의 출력 확인 환경이 필요한 작업은 해당 환경이 관측돼야 완료할 수 있다.',
        '- **범위 결정:** 조건부 작업을 제외할 때는 지정된 범위 결정 ID에 근거를 남긴다. 제외 작업 자체를 완료로 체크하지 않으며, 원래 필수 요구를 자동 축소하지 않는다.',
        '- **단계 종료:** 잎 작업의 개수로 전체 완료율을 추정하지 않는다. Phase 3 최종 수용은 `P3-GATE-03`에서 별도로 판단한다.', '',
        '```powershell',
        'python tools/roadmap.py --next',
        'python tools/roadmap.py --task P3-LNX-01',
        'python tools/roadmap.py --summary --phase 3',
        'python tools/roadmap.py --check',
        '# JSON의 상태/근거를 갱신한 뒤 문서 재생성',
        'python tools/roadmap.py --write',
        '```', '',
        '**표기:** `[x] 완료`는 연결된 근거의 좁은 범위에 한정한다. `[ ] 미완료`와 `[ ] 보류`는 구분한다. '
        '필수/범위 결정 후/후속 최적화/보류 유지도 별도 분류한다.', '',
        '## 단계별 상태', '',
        '| Phase | 완료 | 필수 잔여 | 조건부 잔여 | 후속 최적화 잔여 | 보류 | 상태 해석 |',
        '|---|---:|---:|---:|---:|---:|---|',
    ]
    for phase in data['phases']:
        subset = [t for t in data['tasks'] if t['phase'] == phase['id']]
        c = counts(subset)
        pending = {gate: sum(t['status'] == 'todo' and t['gate'] == gate for t in subset) for gate in ('required', 'conditional', 'followup')}
        lines.append(f"| {phase['id']} — {cell(phase['title'])} | {c['done']} | {pending['required']} | {pending['conditional']} | {pending['followup']} | {c['deferred']} | {cell(phase['scope'])} |")
    task = next_task(data, ids, data['active_phase'])
    lines += ['', '## 다음 권장 작업', '']
    if task:
        lines += [f"**`{task['id']}` — {task['title']}**", '', task['work'], '', '**완료 기준:** ' + task['done_when'], '']
        if task.get('requires'):
            lines += ['**환경 조건:** ' + task['requires'], '']
    else:
        lines += ['자동 선택 가능한 필수 작업이 없다. 미충족 선행 조건·환경·범위 결정·최종 수용 상태를 확인한다.', '']
    lines += ['## 변경하지 않는 경계', '',
              '- 장시간 메모리 수용과 과거 Intel AV 조사는 사용자 지시에 따라 보류한다. 완료나 하드웨어 원인 확정으로 표시하지 않는다.',
              '- Windows Vulkan의 기본 그래픽스 기능과 최근 생성/로더/할당 장애 검증은 완료 근거를 보존한다. 미완료 Linux 검증과 혼합하지 않는다.',
              '- 버퍼 풀은 실험 후보다. sparse 예약 6.2배 관측은 유지하며 제품 dedicated 기본 정책은 그대로다. 후보 채택은 Phase 6의 별도 결정이다.',
              '- DXC/일반 reflection/MSL 변환은 Phase 5, allocator/upload ring/cache 최적화는 Phase 6, 전체 배포는 Phase 7이다. Linux native 설치 smoke 등 Phase 3 수용에 필요한 좁은 작업은 해당 ID에서 검사한다.',
              '- Compute/render graph, stencil/MSAA/mip/array 등 초기 공통 RHI 밖 확장을 Phase 3 필수로 임의 추가하지 않는다. 후속 릴리스 범위는 기존 역사 문서에 남긴다.', '']
    for phase in data['phases']:
        lines += [f"## Phase {phase['id']} — {phase['title']}", '', phase['scope'], '']
        for group in [g for g in data['groups'] if g['phase'] == phase['id']]:
            lines += ['### ' + group['id'] + ' — ' + group['title'], '', '| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |', '|---|---|---|']
            for task in [t for t in data['tasks'] if t['group'] == group['id']]:
                checked = 'x' if task['status'] == 'done' else ' '
                a = f"[{checked}] `{task['id']}`<br>{STATUS[task['status']]} · {GATE[task['gate']]}"
                b = '**' + task['title'] + '**<br>' + task['work'] + '<br>완료: ' + task['done_when']
                c = '선행: ' + (', '.join('`' + dep + '`' for dep in task['depends_on']) or '없음')
                if task.get('requires'):
                    c += '<br>환경: ' + task['requires']
                if task.get('scope_from'):
                    c += '<br>범위 결정: `' + task['scope_from'] + '`'
                if task.get('reason'):
                    c += '<br>보류 사유: ' + task['reason']
                if task['evidence']:
                    c += '<br>근거: ' + ', '.join('[' + pathlib.Path(link).stem + '](' + pathlib.Path(link).relative_to('docs').as_posix() + ')' if link.startswith('docs/') else '[' + pathlib.Path(link).name + '](../' + link + ')' for link in task['evidence'])
                lines.append('| ' + ' | '.join(cell(x) for x in (a, b, c)) + ' |')
            lines.append('')
    return '\n'.join(lines).rstrip() + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--check', action='store_true')
    action.add_argument('--write', action='store_true')
    action.add_argument('--summary', action='store_true')
    action.add_argument('--next', action='store_true')
    action.add_argument('--task')
    parser.add_argument('--phase', type=int, choices=range(8))
    args = parser.parse_args()
    data, ids = load()
    phase = data['active_phase'] if args.phase is None else args.phase
    if args.check or args.write:
        expected = render(data, ids)
        if args.write:
            DOCUMENT.write_bytes(expected.replace('\n', '\r\n').encode('utf-8'))
        else:
            require(DOCUMENT.read_text(encoding='utf-8') == expected, 'ROADMAP.md is stale; run --write')
        print(f"PASS {len(ids)} task IDs; evidence, dependency graph, historical text and rendered roadmap checked")
    elif args.task:
        require(args.task in ids, 'Unknown task ID: ' + args.task)
        print(json.dumps(ids[args.task], ensure_ascii=False, indent=2))
    elif args.next:
        task = next_task(data, ids, phase)
        print(json.dumps(dict(phase=phase, readiness='선행 ID 기준이며 실행 환경 검증을 대체하지 않음', task=task), ensure_ascii=False, indent=2))
    else:
        tasks = [t for t in data['tasks'] if args.phase is None or t['phase'] == phase]
        pending = {gate: sum(t['status'] == 'todo' and t['gate'] == gate for t in tasks) for gate in ('required', 'conditional', 'followup')}
        print(json.dumps(dict(counts=counts(tasks), pending_by_gate=pending, phases=[dict(phase=p['id'], **counts([t for t in tasks if t['phase'] == p['id']])) for p in data['phases'] if args.phase is None or p['id'] == phase]), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError) as exc:
        print('Roadmap error: ' + str(exc), file=sys.stderr)
        raise SystemExit(2)
