# Phase 3 — Windows/Linux 전체 Python 검증 정합성

2026-10-25 · `P3-LNX-14` · 검증 소스 커밋 `35d0808`.

Windows와 Linux의 **새 CI checkout에서 전체 Python 검사 640개씩, 합계 1,280개가 통과**했다. 각 플랫폼의 subtest는 540개이며 오류·실패·건너뜀·예상 실패·예상 밖 성공은 모두 0이다. 기존 627개 테스트 ID를 전부 유지하고 관련 회귀 검사 13개를 추가했다.

[전체 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40194816503)의 [Windows job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40194816503/job/121173274441)과 [Linux job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40194816503/job/121173274276)이 모두 성공했다. 같은 소스의 [Linux Vulkan compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40194816496)도 성공했다. [요약](../benchmarks/reports/2026-10-25-phase3-python-ci/summary.json), [Windows receipt](../benchmarks/reports/validation/2026-10-25-phase3-python-ci/native/windows/receipt.json), [Linux receipt](../benchmarks/reports/validation/2026-10-25-phase3-python-ci/native/linux/receipt.json)에 결과를 보존했다.

## 수정 전 실패와 원인

초기 614개 검사 로그와 직전 `P3-LNX-12`의 627개 검사 로그를 그대로 보존했다. [수정 전 근거 목록](../benchmarks/reports/validation/2026-10-25-phase3-python-ci/before-evidence.json)에 각 원본의 경로·소스 커밋·CI run·bytes·SHA-256을 기록했고, 수정 전 Git tree의 바이트와 다시 대조했다. 과거 실패 보고서를 성공 결과로 덮어쓰지 않았다.

| 검사 시점 | Linux | Windows |
|---|---:|---:|
| 기존 614개 baseline | errors 92 | errors 99 |
| 직전 627개 baseline | errors 92 | errors 99 |
| 수정 후 전체 640개 | **오류·실패·건너뜀 0** | **오류·실패·건너뜀 0** |

원인은 세 가지였다.

1. **두 플랫폼 공통 92건**: loader fault 40개, pool churn 24개, rebased mixed fault 28개의 과거 Windows 실행 기록을 검증하면서, 현재 checkout의 source fingerprint를 요구했다. 당시 파일은 LF/CRLF가 섞인 원본 snapshot이며 새 checkout의 바이트와 다르다. 실제 계약 위반과 실행 자료의 소스 범위 차이를 구분하지 못한 것이다. 이 때문에 기존 변조 대조도 의도한 내부 계약 검사보다 먼저 source identity에서 실패할 수 있었다.
2. **Windows 추가 5건**: 임시 root의 짧은 이름 `RUNNER~1`은 그대로 두고 입력 경로만 긴 이름 `runneradmin`으로 resolve한 뒤 `relative_to`를 호출해 같은 위치를 저장소 밖으로 오판했다.
3. **Windows 추가 2건**: 고정 작업의 저장된 명령에는 짧은 임시 출력 경로가 있고, 다시 만든 기대 명령에는 긴 경로가 있어 동일한 실행 대상을 다른 명령으로 판정했다.

기존 로컬 Windows 작업 트리에서는 627개가 통과했다. 이 관측도 [로컬 수정 전 로그](../benchmarks/reports/validation/2026-10-25-phase3-python-ci/local-before.log)에 보존한다. 로컬 통과를 새 checkout의 성공 근거로 대체하지 않았으며 최종 수용은 위의 실제 Windows/Linux CI다.

## 과거 실행 자료와 현재 실행의 소스 범위 분리

[`tests/recorded_sources.json`](../tests/recorded_sources.json)은 기존에 보존된 소스 ZIP 3개의 전체 해시와 실제 fingerprint 입력 파일의 해시를 고정한다. [`recorded_sources.py`](../tests/recorded_sources.py)는 ZIP과 member·경로·중복·크기를 확인하고, 필요한 파일만 임시 디렉터리에 원본 바이트 그대로 복원한다. 세 테스트 클래스는 검증 중인 runner의 `ROOT`를 이 소스 fixture로 잠시 지정하고 검사 후 복구한다.

| 보존된 소스 | 실제 fingerprint 입력 | 당시 source fingerprint |
|---|---:|---|
| Loader ownership fault | 8개 | `5633d2982f895be2442e5bfd04e119091b7707c41e84af1dc829e2201aa802db` |
| Rebased pool churn | 13개 | `8ede9ef8409794e84ee653054757a2aaa0ae56a7d00287b0d4662d7f6c0da047` |
| Rebased mixed fallback fault | 11개 | `843539219be79a401db1162523622d3604e75da9b4defca3368aeb429a744831` |

Fingerprint 반환값을 mock하거나 결과 JSON의 fingerprint를 바꾸지 않는다. **기존 실제 실행용 validator 4개와 fingerprint 계산은 변경하지 않았다.** 기본 실행은 계속 선택된 현재 소스의 정확한 바이트를 요구한다. 과거 기록의 SDK·장치·메모리 보존·정확한 readback·fault injection·binding/snapshot·native cleanup 계약도 같은 validator로 검사한다. 과거 결과를 현재 Linux 하드웨어 실행으로 재분류하지 않는다.

필수 과거 matrix가 없으면 세 테스트 클래스는 이제 skip 대신 실패한다. Archive/member/aggregate 해시 변조, 중복·빈 source 목록, 경로 이탈, 누락 archive를 거부하는 대조를 추가했다. 각 소스 snapshot에서 기존 실제 결과가 통과한 뒤 **파일의 CRLF만 LF로 바꾸면 기존 production validator가 거부**하는 대조도 통과했다. Source identity 검사를 완화하거나 줄바꿈을 정규화해 과거 결과를 받아들이지 않는다.

## Windows 경로 비교 수정

[`run_memory_stress.py`](../tools/run_memory_stress.py)는 소스 복사 경로를 계산할 때 입력과 root를 모두 resolve한다. 같은 임시 위치의 별칭을 일관되게 비교하고, 실제 root 밖의 소스 경로는 여전히 거부한다. 기존 frozen input 해시, serial 실행, guard·native crash·timeout·별도 WARP 실행 및 명시적 거부 receipt 검사는 유지했다.

[`run_fixed_memory.py`](../tools/run_fixed_memory.py)는 저장된 명령의 **실행 파일과 출력 파일 두 위치만 canonical 경로로 비교**한다. 인자 개수·문자열 타입·flag·순서·모드·작업량은 계속 정확히 일치해야 한다. 원래 저장된 argv는 바꾸지 않는다. Artifact 해시·core library·정책·환경·reference 검증도 그대로다.

추가 회귀 13개는 소스 복원·변조 대조 10개, 고정 작업 명령의 같은 경로/다른 binary·output·인자 순서·타입·추가 인자 검사 2개, root 별칭에서도 frozen source receipt를 보존하는 검사 1개다. 앞 단계의 설치 감사 13개를 포함해 기존 627개도 모두 실행했다. 이 검사는 CPU/합성 자료의 검증이며 메모리·Intel 충돌의 실제 조사를 재개한 것이 아니다.

## 전체 CI 기록과 소스 바이트 대조

[`check_python_contracts.py`](../tools/check_python_contracts.py)는 기존과 같은 `test_*.py` 전체 discovery를 실행하고 UTF-8 원본 로그와 receipt를 보존한다. 테스트 제외 목록은 없다. 실행한 ID·subtest 수·실패/오류/skip 목록, Python/OS, Git 소스 커밋, 도구·테스트의 Python/JSON과 SDK·CI 설정 174개의 실제 파일 해시, 실행 전후 SDK fingerprint와 tracked 상태를 기록한다. 실행 중 이 입력이 바뀌면 성공을 거부한다.

두 CI checkout 모두 실행 전후 tracked 상태가 비어 있고 입력 바이트가 같았다. Linux는 Python 3.12.3, Windows Server 2022는 Python 3.12.10이다. 640개 ID와 순서가 두 플랫폼에서 일치하며 이전 614개 ⊆ 627개 ⊆ 640개임을 실제 로그/receipt에서 확인했다. 오류를 테스트 삭제나 제외·skip으로 감추지 않았다.

| 소스 바이트 범위 | SDK fingerprint |
|---|---|
| 이번 Linux 새 checkout, 27개 SDK 파일 | `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830` |
| 이번 Windows CI 새 checkout, SDK 파일 모두 CRLF | `09ce93c4405f86cd69b9f6ac1484041bfe42466cb40d8f05767d987fc86a2da5` |
| 기존 로컬 Windows 작업 트리·이전 고정 SDK 결과 | `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401` |

각 CI fingerprint는 receipt의 SDK 파일 27개 해시로 다시 계산했다. Linux 입력 174개는 검증한 Git blob 바이트와 일치했다. Windows는 28개가 Git blob 그대로이고, 146개는 `.gitattributes`가 허용한 CRLF checkout 바이트다. `-text` 또는 `eol=lf` 파일에 임의 변환을 허용하지 않았다. SDK C++·공개 API·root `CMakeLists.txt` 자체는 수정하지 않았다.

**세 fingerprint는 실제 다른 checkout 바이트의 관측값으로 유지한다.** 새 Windows CI 값을 기존 Windows GPU 결과에 덮어쓰지 않으며, 과거 고정 자료나 source ZIP도 재작성하지 않는다. 메모리 정책 역시 변경하지 않았다.

GitHub artifact의 SHA-256과 파일 바이트, 원본 job log의 receipt와 내려받은 receipt, 검사 입력과 Git 소스, 과거 baseline과 ZIP/member를 독립적으로 대조했다. [원본 대조 도구](../benchmarks/reports/validation/2026-10-25-phase3-python-ci/verify_evidence.py)를 전체 저장소에서 다시 실행할 수 있다. 실제 소스 ZIP이 필요하며 별도 GPU 실행은 필요하지 않다.

```sh
python benchmarks/reports/validation/2026-10-25-phase3-python-ci/verify_evidence.py
```

새 checkout에서 전체 Python 검사를 재현하려면 출력 폴더가 없는 상태에서 다음을 실행한다. Python 3 표준 라이브러리를 사용한다.

```sh
python tools/check_python_contracts.py --output out/python-contracts
```

같은 전체 CI에서 Windows CTest 37개·설치·라이선스 감사·설치 소비자와 Linux portable CTest 11개도 통과했다. 별도 Vulkan compile CI는 두 preset CPU 검사 22개를 통과했다. Windows C++ CI의 기존 `memory_reclamation_(gpu|validation)` 제외 정책은 변경하지 않았고 Python에는 제외가 없다. Hosted CI를 물리 GPU/GBV·새 성능·장시간 메모리 수용으로 해석하지 않는다.

`P3-LNX-14`는 이 전체 Python/CI 정합성 범위에서 완료다. 물리 Linux GPU의 공통 suite와 validation은 최종 gate의 필수 의존성 `P3-LNX-15`, Linux WSI는 `P3-WSI-01`~`08`에 남는다. 메모리·Intel 충돌 원인 조사는 사용자 지시대로 보류하며 하드웨어 원인으로 확정하지 않는다.
