# P3-BEN-08 — Linux benchmark 4종 build·software 준비 검증

2026-10-28. **업로드·draw·PSO·자원 생성 소비자의 Linux 이식과 실제 software 실행 검증을
마쳤다. P3-BEN-08은 선행 물리 GPU qualification 부재로 todo를 유지한다.**
사용 가능한 Linux GPU 환경이 없다는 사용자 확인을 반영해 software 준비 검증을 진행했다.

[검증 요약](../benchmarks/reports/2026-10-28-phase3-linux-workloads/summary.json)과
[원본 receipt](../benchmarks/reports/validation/2026-10-28-phase3-linux-workloads/native/linux-workloads-evidence/receipt.json)는
`P3_BEN_08_completed=false`, `physical_GPU_accepted=false`, `performance_samples=0`,
`all_workload_scenarios_accepted=false`를 보존한다. P3-BEN-07·P3-LNX-15·P3-BEN-09와
전체 Linux 성능 비교의 수용 상태도 변경하지 않았다.

## 이식 방법과 입력 경계

[`port_linux_benchmarks.py`](../tools/port_linux_benchmarks.py)는 고정된 Windows 소비자에서
Linux 소스를 결정적으로 생성한다. 변경 지점은 platform include, validation library 조회,
logical device 생성 전의 정확한 UUID/장치 종류 guard, Linux identity attribution,
null LUID 다섯 곳이다. SDK/SDL의 자원·shader·명령·타이머·readback 본문은 재사용한다.
원본 구조가 달라져 변환 지점을 정확히 찾지 못하면 실패한다.

[`Platform.hpp`](../benchmarks/linux_workloads/Platform.hpp)와
[`Environment.cpp`](../benchmarks/linux_workloads/Environment.cpp)는 기존 Linux native
UUID·환경 helper를 사용한다. SDL은 공개 장치 이름을 단일 native 장치에 연결하며
native handle을 직접 검증했다는 주장은 하지 않는다. 중복 이름, 다른 UUID, software/physical
종류 불일치는 거부한다. Linux LUID는 합성하지 않고 JSON null로 기록한다.

[`linux_workloads.py`](../tools/linux_workloads.py)는 **같은 revision의 BEN-07 qualification**과
고정 runtime/SDK/SDL 입력을 요구한다. 새 소비자는 qualified installed SDK와 static SDL을
사용하며 package 경로가 해당 root 안에 있는지 검사한다. 생성한 C++/manifest, 실제 compile
명령과 binary 해시, validation 설정, 실행 명령·환경·PID·직렬 구간·원본 출력을 보존한다.
설정과 입력은 각 실행 전후 그대로여야 한다.

기존 Windows 소비자 소스·CMake와 SDK 제품 소스 28파일은 변경하지 않았다.
사용자 수정이 있는 reference test target도 빌드하지 않았다. Linux SDK fingerprint는
`fdcb274bf57381a3b426f3c22b420d6719c3ba37e4ba038b6902a4b2bebb6f79`다.

## 실제 native 실행

실행 소스 `e4c1e3472be9bd2547ea42627993fa2cd1916dab`, Ubuntu hosted Linux x86_64,
kernel `7.0.0-1017-azure`, GCC `14.2.0`, AMD EPYC 7763 환경에서 검증했다.
장치는 llvmpipe `LLVM 20.1.2`, native CPU 종류 4이며 device UUID는
`6d65736132352e322e382d3075627500`, driver UUID는 `6c6c766d706970655555494400000000`이다.
SDK ID는 큰 정수 손실을 막기 위해 문자열 `5892941257450743403`으로 보존한다.

SDL `3b2bc951500ca6a1f428af77e036e732d9a8afe3`, Vulkan-Headers
`b379292b2ab6df5771ba9870d53cf8b2c9295daf`와 고정 VVL 1.3.290/runtime lock을 사용했다.
BEN-07의 새 probe qualification 이후 동일 입력으로 네 실행 파일을 빌드했다.

| 실행 | 범위 | 결과 |
|---|---|---|
| no-work probe 8개 | 4종 × SDK/SDL | 표본·readback 0, 같은 장치·driver·source 확인 |
| required validation smoke 8개 | odd upload / draw1 / pso1 / buffer1 × SDK/SDL | 각각 1 warmup + 3 진단 반복, 전체 참조 일치 |
| 거부 확인 24개 | 잘못된 UUID / 누락 layer / 다른 장치 종류 × 4종 × SDK/SDL | exit 1·빈 stdout·지정된 실패 이유 일치 |

총 이미지 **24회**, 버퍼 **8개**, 논리 데이터 **263,648바이트**를 검증했다.
warmup 8회와 진단 시간값 **24행**은 validation 출력으로 보관하고 성능 표본에 합산하지 않는다.
P50/P95/P99·SDK 순위는 계산하지 않았다. 각 프로그램의 나머지 사례는 컴파일했지만 이번 대표
smoke로 전체 사례 수용을 완료하지 않는다. 기존 BEN-07 qualification process 수도 이 40개에
중복 합산하지 않았다.

SDL upload의 기본 instance API 요청과 draw/PSO/자원 생성의 명시적 1.1 요청을 유지했다.
SDK general buffer와 SDL vertex buffer의 용도 차이, 공개 release·driver cache·실제 메모리
회수의 경계도 유지한다. CPU fence wall time은 GPU timestamp가 아니다.

## 원본·재현 검증

- 새 검사 13개와 [전체 Python 회귀 889개](../benchmarks/reports/validation/2026-10-28-phase3-linux-workloads/python-local/receipt.json)가
  실패·오류·건너뜀 0으로 통과했다. 원본 Windows workload 본문 보존과 변환의 결정성도 검사했다.
- 같은 소스의 [Linux 소비자 build/probe/smoke CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40618574760)와
  [Windows·Linux 회귀 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40618574763)가 성공했다.
- [원본 artifact ZIP](../benchmarks/reports/validation/2026-10-28-phase3-linux-workloads/github-artifact.zip)은 GitHub digest
  `sha256:f2e123ac5bf1eac54407d2704da8df5991a8e0a17d1158449368860faa7f6a57`와 일치한다.
  ZIP을 풀어 보관한 파일과도 byte 단위로 대조한다. 외부 실행 파일·library는 배포하지 않았다.
- [65파일 source capsule](../benchmarks/reports/validation/2026-10-28-phase3-linux-workloads/source-manifest.json)은 실행 시
  source input 해시와 모두 일치한다. 기존 attribute에 따라 Linux에서도 CRLF로 checkout되는
  원본 6개 파일은 그 바이트를 복원했다. SDK 28파일은 직전 source ZIP과 동일하다.
- 캡슐에서 C++ 4개를 다시 생성해 실제 컴파일 source와 대조했다. 소스 해시·검증 설정·선택·
  readback·명령·순서·환경·누락·중첩·control 및 완료/성능/전체 사례 승격 등 **19개 변조**를
  거부했다. Linux 경로 정렬과 원본 줄바꿈을 보존해 Windows에서도 재검증한다.

```text
python benchmarks/reports/validation/2026-10-28-phase3-linux-workloads/verify.py --json
```

이 offline 검사는 GPU나 native 실행 파일을 실행하지 않는다. 보관 근거가 있는 커밋의
Windows·Linux CI도 독립적으로 같은 검사를 수행한다.

## 남은 수용과 실행 준비

[재현 명령·제약](../benchmarks/linux_workloads/README.md)과
[physical 수동 workflow](../.github/workflows/linux-comparison-physical.yml)를 준비했다.
수동 workflow는 dispatch하지 않았다. 실제 GPU 환경이 생기면 같은 source의 물리 BEN-07
qualification에 이어 네 소비자를 build/probe하고 근거를 대조해야 한다.

**P3-BEN-07·08은 todo, 전체 Linux 사례·성능 비교도 별도 미완료**다. 이번 software 성공으로
필수 gate를 줄이거나 물리 GPU·WSI·장시간 메모리/Intel 충돌 수용을 대신 완료하지 않았다.
