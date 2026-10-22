# Phase 2 실제 runtime·어댑터 선택과 GBV 재현 비교

2026-10-22. [이전 장치 수명 검증](PHASE2_DEVICE_LIFETIME_REPORT.md)을 이어서 실제 D3D12
runtime과 하드웨어 어댑터를 선택·검증하는 독립 도구를 구현했다. **공식 Agility 안정판과 다른
GPU에서도 메모리 증가가 재현됐다.** 원래 장시간 GBV 메모리 gate는 미완료로 유지한다.
SDK 구현/API/기본 runtime/검증 설정과 기존 실패 기준을 바꾸지 않았다.

## 추가한 도구

`Runtime.cpp`/`gbv_runtime_probe`는 GraphicsSDK header/library 없이 Windows SDK로 빌드한다.
세 호출 경로를 별도 process에서 비교한다. 기본 `D3D12CreateDevice`, 기본 runtime의 device
factory, 선택한 Agility runtime의 device factory다. factory의 debug interface를 통해 검증을
설정한다. [Microsoft independent device 계약](https://microsoft.github.io/DirectX-Specs/d3d/IndependentDevices.html)에
따른 선택이며 SetSDKVersion/ApplyToGlobalState나 실험 기능, Developer Mode 설정 변경은 사용하지 않는다.

요청한 경로·버전만 기록하지 않는다. 실제 device configuration의 SDKVersion, debug/GBV flags,
GPU validation flags, experimental feature 수와 실제 device LUID를 확인한다. Windows COM의
GetDesc 구현 주소가 속한 module 경로와 로드된 runtime DLL도 기록한다. 선택한 Core/SDKLayers
한 쌍과 provider가 일치해야 하며, inbox fallback이나 섞인 DLL, state tracking 생략을 거부한다.
factory 사용 자체의 영향을 구분하기 위해 기본 API와 inbox factory를 별도 대조로 둔다.

`--adapter-index`는 DXGI의 non-software 어댑터를 순서대로 선택한다. 실제 device LUID와 비교하며,
없는 index에서는 실패한다. WARP는 EnumWarpAdapter로 별도 선택한다. 각 profile의 하드웨어
이름/LUID와 기본 API·factory의 실제 inbox SDKVersion은 같아야 한다.

각 process는 debug text 저장 한도를 8로 낮추고 경고 64개로 포화시킨 뒤 의도적인 ERROR 하나를
주입한다. unfiltered callback이 그 ERROR를 세고 text clear 뒤에도 누적 1을 유지하는지 확인한다.
그 뒤 작업에서 누적 값이 1보다 커지면 실패한다. **intentional 1과 unexpected 0을 구별한다.**
오류 관측 범위는 callback 등록부터 native device teardown 직전까지다.

작업은 alias 보유/마지막 해제 대조 1개와 512회 × RT RGBA8/D32 depth/일반 RGBA8 texture
세 개의 생성·해제다. 모두 64×64이며 render/command submit/shader 작업은 없다. 매 resource의
실제 파괴 callback을 확인하고 64회마다 checkpoint를 기록한다. device 파괴와 200ms 뒤 관측도
분리한다. 초기값 대비 private commit 증가가 512MiB를 넘으면 추가 작업을 중단한다.
이 유한 생성 검사를 장시간 plateau나 렌더링 정확성 검증으로 사용하지 않는다.

## 고정 runtime과 출처

OS는 Windows 11 build 26220이고 inbox Core/SDKLayers 파일 버전은 10.0.26100.9739이며 실제
SDKVersion은 616이다. 비교 대상은 [공식 NuGet Microsoft.Direct3D.D3D12 1.619.6](https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.619.6),
실제 SDKVersion 619, 파일 버전 1.619.6.0.20260914.1이다. 두 local DLL의 Microsoft 서명이 Valid임을 확인했다.

[고정 목록](../tools/memory_validation_probe/agility-runtime.lock.json)은 공식 package URL, 전체
package SHA-256, x64 Core/SDKLayers 및 license SHA-256을 고정한다. `prepare_runtime.py`는
같은 pin을 검증한 뒤 지정한 DLL/metadata만 새 output 폴더에 준비한다. Windows용 binary에는
Microsoft DirectX 약관이 적용되며 MIT header 약관과 구별한다. package header는 컴파일하지
않고 runtime binary/package도 SDK ZIP이나 Git/source archive에 재배포하지 않는다.
원본 package와 LICENSE는 local `out/`에 보존하고 실행 증거에는 출처·해시를 기록한다.

## 최종 관측

NVIDIA GeForce GTX 1660 SUPER driver 32.0.16.2101, Intel UHD Graphics 770 driver
32.0.101.6129를 확인했다. 각 어댑터마다 3 runtime 경로 × 3 mode를 새 process로 순차 실행했다.
`adapter-0`/`adapter-1`이 최종 고정 source의 18개 process다. 앞선 `campaign`/`final-campaign`은
어댑터 선택·검사 보완 전의 NVIDIA 탐색이며 최종 실행 합계에서 제외한다.

아래는 counter 대조 후→resource 파괴 완료 시점의 **관측 증가량**이다. 아직 device가 살아
있는 구간이며 초기 runtime/driver 비용을 모두 제거한 순수 SDK 비용이나 연속 peak가 아니다.
CPU debug 모드에는 GBV를 요청하지 않고, hardware GBV/WARP GBV에는 state tracking을 유지한다.

| 어댑터 / mode | 기본 API 616 private | inbox factory 616 private | Agility factory 619 private |
|---|---:|---:|---:|
| NVIDIA CPU debug | 0.117MiB | 0.117MiB | 0.117MiB |
| NVIDIA hardware GBV | 311.816MiB | 311.059MiB | 311.883MiB |
| Intel CPU debug | 4.129MiB | 4.129MiB | 4.129MiB |
| Intel hardware GBV | 149.934MiB | 149.914MiB | 149.930MiB |
| WARP GBV, NVIDIA profile 대조 | 1.227MiB | 1.168MiB | 1.312MiB |
| WARP GBV, Intel profile 대조 | 1.227MiB | 1.176MiB | 1.164MiB |

NVIDIA hardware GBV는 세 경로 모두 DXGI nonlocal 증가 309.828MiB/local 증가 0,
Intel hardware GBV는 local 증가 112.137MiB/nonlocal 증가 0이었다. 두 segment를 모두 원본에
기록한다. Intel의 nonlocal 0만으로 GPU 관련 증가가 없다고 판단하지 않는다. WARP의 0도
물리 메모리 사용량 0의 증명이 아니다. hardware query 실패와 null/값 불일치는 거부한다.

각 process는 1,537개 texture 파괴와 native device 파괴 1회를 확인했다. 총 **27,666개 resource,
18개 device 파괴 callback, intentional ERROR 18개, unexpected ERROR/CORRUPTION 0**이다.
정상 종료 뒤 local/nonlocal CurrentUsage가 모두 0인 checkpoint를 관측했다. 원래 CPU 초기값으로
돌아오거나 무제한 반복에서 메모리 plateau를 이루었다는 뜻은 아니다.

관측상 증가는 특정 NVIDIA 어댑터 하나, inbox runtime 616 하나 또는 기본 생성 API 하나에만
한정되지 않았다. Agility 1.619.6으로 바꾸는 것만으로 이 재현이 해결되지는 않았다. 이는 두
driver/한 OS의 유한 관측이며 정확한 내부 원인, 모든 driver/runtime의 동작, SDK의 누수 부재를
증명하지 않는다. 검증을 줄이거나 자동 device 재생성을 SDK 우회책으로 넣지 않았다.

## 회귀·재현과 남은 범위

[실행 방법](../tools/memory_validation_probe/README.md)의 fresh output 경로로 재현한다.
`runtime_compare.py`는 source/binary/SDK fingerprint/선택한 package와 system DLL들을 실행 전에
고정하고 끝난 뒤 재확인한다. 실제 provider/version/module/GBV/adapter, complete callback 수,
intentional 오류 대조와 누락 없는 checkpoint를 검사한다. `memory_acceptance_claim=false`이며
기존 memory policy는 변경하지 않는다. 잘못된 runtime path/version/adapter index의 실제 native
실행 3개는 성공 footer 없이 실패했다. 조용한 fallback으로 성공 처리하지 않았다.

Runtime.cpp/Lifetime.cpp/전용 CMake 세 파일만 SDK가 없는 별도 폴더로 복사해 독립 빌드했다.
그 binary의 Intel Agility GBV 64회 실행에서도 별도 texture 193개/device 1개 파괴와 오류 대조를
확인했다. 이 실행은 위 18-process 합계와 구별한다.

core CTest 22/22, D3D12-OFF 4/4, Python 102/102가 통과했다. 추가 Python 회귀 11개는 잘못된
provider/fallback/mixed modules, 검증 축소, 오류 대조 누락, 파괴/참조 부족, matrix 혼합 및
변조된 package 등을 거부한다. 최종 자체 compiler/linker inventory는 25 inputs/1 component,
runtime probe는 SDK library를 링크하지 않는다. 이 inventory의 MIT 표시는 자체 C++ 소스의
범위이며 별도 Microsoft runtime의 라이선스를 바꾸지 않는다.

SDK source fingerprint는 `833978d58dc9e18daf939d12ef24eb69ca965b323caab41fd30682131baa89f1`로
그대로다. SDK/common 감사와 기존 SDK package 내용 검사를 통과했고 6개 기존 SDK ZIP 및 과거
source archives/실패 gate를 보존했다. 공통 benchmark 72개, 설치 consumer, 이전 장시간 측정과
재사용/수명 결과는 해시로 확인하며 새로 실행한 증거로 표현하지 않는다.

[원본·보존 source](../benchmarks/reports/validation/2026-10-22-phase2-runtime-comparison/)와
[최종 요약·해시](../benchmarks/reports/2026-10-22-phase2-runtime-comparison/)를 함께 확인한다.
남은 범위는 원래 반복 생성 장시간 GBV 메모리 gate, 다른 OS/driver 대조, interactive presentation,
예상치 못한 TDR/hot-unplug 및 native Vulkan/Linux/macOS/Metal이다. 이번에 새 성능 비교나
release 완료를 주장하지 않는다.
