# Phase 3 — Vulkan buffer transfers

2026-10-23, Windows x64 / MSVC 19.44 / Windows SDK 10.0.26100.0.

사용자 요청에 따라 장시간 메모리 acceptance와 과거 Intel AV 원인 조사는 보류하고,
Phase 3의 첫 SDK 구현을 진행했다. 하드웨어 원인으로 확정하거나 두 항목을 통과로
바꾸지 않았다. 과거 정책·실패 관측·원본 패키지는 보존했다.

## 구현

`GFX_ENABLE_VULKAN=ON`으로 공용 `gfx` API에 Vulkan 1.1 device/exact-adapter,
upload/device/readback buffer, explicit copy state, queue/fence를 연결했다.
OS loader를 동적으로 사용하며 SDK public header에는 Vulkan 의존성이 없다.
UUID 기반 ID 충돌/종류 불일치/없는 ID는 실패하며 다른 GPU로 바꾸지 않는다.

명령 검증을 참조 보유 전에 끝내고, CPU write/submit에 따른 version stale을 거부한다.
명령과 제출은 public wrapper 대신 native allocation을 보유한다. 공용 장치가 종료되면
실행을 drain하고 native 자원을 retire하여 생존 child의 native 연산을 거부한다.
전체 할당 map/flush/invalidate, transfer/host barrier, finite timeout 범위를 구현했다.
미구현 기능은 `Unsupported`와 false capability로 드러난다. [계약](VULKAN.md)에
지원 범위와 primary Vulkan 명세 링크를 기록했다.

## 실제 검증

| 구성 | 실제 결과 |
|---|---|
| D3D12 ON + Vulkan ON | 35/35 CTest 통과, skip 없음 |
| D3D12 OFF + Vulkan ON | 7/7 CTest 통과, skip 없음 |
| 두 backend OFF | 6/6 CPU CTest 통과, skip 없음 |
| 설치 SDK만 사용하는 별도 find_package 앱 | 같은 Vulkan 계약 suite 통과 |
| 기존 Python 도구 계약 | 208/208 통과 |
| Vulkan validation 요청 | 미설치 `VK_LAYER_KHRONOS_validation` 때문에 명시적 실패; validation 통과가 아님 |
| 누락 헤더 구성 | configure 실패를 확인 |

실제 후보는 NVIDIA GeForce GTX 1660 SUPER와 Intel UHD Graphics 770이며,
각 구성/설치 소비자에서 두 장치를 exact-ID로 생성했다. Vulkan native suite 하나당
717개 assertion, 48회 upload→device→readback, **57,042,546바이트**의 완전 비교를
통과했다. 1/3/4/257/4097/65539/1048579/8388611바이트 크기마다 세 payload를 사용했다.
통합·Vulkan-only·설치 소비자 총 144회/171,127,638바이트이며 성능 측정은 아니다.

추가 대조는 bounded host slice/overflow, alias/null/foreign/state 거부, 거부 뒤 native
할당 수·바이트 불변, CPU write/다른 submit 뒤 stale recording, wrapper 소멸 뒤 native
할당 보유, 미제출 기록 해제, 완료 뒤 잘못된 timeout/foreign/future fence,
creating-thread와 public-device retirement를 검사한다. Zero poll은 완료 또는 timeout을
허용하고 같은 fence를 재시도한다. 이 suite는 결정적인 GPU gate를 삽입하지 않으므로
모든 실행에서 실제 timeout이 발생한다고 주장하지 않는다.

[원본 명령·로그](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-transfer/commands.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-transfer/summary.json),
[해시/소스 ZIP manifest](../benchmarks/reports/2026-10-23-phase3-vulkan-transfer/evidence.json)를 보존한다.
최종 SDK source fingerprint는
`617ac17a94fe438d25f131ea63e737c2f38d0395998b55184a419d027ec50701`이다.

## 배포/감사

새 로컬 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-transfer.zip`이다.
실제 compiler-read 60개 입력(고정 Vulkan 헤더 11개 포함), 2개 component,
17개 compiler log와 16개 executable linker log를
[새 inventory](../licenses/generated/windows-sdk-vulkan-transfer-release/inventory.json)에 기록했다.
현재 설치 library/public header와 build 일치, 패키지의 정확한 파일·내용·고지를 검사한다.
고정 헤더는 Apache-2.0이며 자체 MIT 소스로 재분류하지 않는다.
Loader/ICD/driver/validation layer나 검증 실행 파일을 SDK/source ZIP에 넣지 않는다.
이전 D3D12 binary·benchmark·감사·SDK ZIP은 역사적 snapshot으로 보존한다.

## 남은 범위

Vulkan layer execution, noncoherent memory 타입의 실제 하드웨어 분기, CPU 소프트웨어
Vulkan 장치, 실제 장치 손실은 이 장비에서 검증하지 않았다. Windows의 두 실제 GPU에서
관측한 범위만 통과다. Vulkan textures/render passes/pipelines, SPIR-V, bindings,
timestamps, presentation, Linux native compile/runtime와 matching competitor 측정이
후속 개발이다. Phase 3 전체 및 v0.1 완료로 표시하지 않는다. 보류한 D3D12 장시간
메모리/Intel 원인과 interactive/physical acceptance도 그대로 미해결이다.
