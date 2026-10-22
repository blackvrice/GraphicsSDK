# Phase 2 native 파괴 관측과 SDK 장치 수명 회귀

2026-10-22. [이전 GBV 분리](PHASE2_GBV_RECLAMATION_REPORT.md)를 이어서 실제 native 자원·장치
파괴 관측과 SDK generation 생성·해제 반복 검사를 구현했다. **수명 계약은 통과했으며 원래
장시간 GBV 메모리 gate는 여전히 미완료다.** SDK 구현/API/기본 검증 설정은 바꾸지 않았다.
SDK fingerprint는 `833978d58dc9e18daf939d12ef24eb69ca965b323caab41fd30682131baa89f1`이다.

## 구현과 측정 범위

`gbv_lifetime_probe`는 GraphicsSDK header/library 없이 빌드되는 native 재현이다. 별도의
`sdk_lifetime_probe`만 기존 SDK library를 링크한다. 새 compiler/linker 감사는 이 구분을 검사한다.
각 case는 새 process, 각 cycle은 새 native device이며 모든 실행은 순차 수행한다.

native 도구는 [ID3DDestructionNotifier](https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/nn-d3dcommon-id3ddestructionotifier)의
파괴 callback을 resource와 device에 등록한다. callback 안에서는 객체/API를 호출하지 않고
process 수명 동안 유효한 atomic counter만 증가시킨다. timeout이나 실패 뒤 늦은 callback에도
저장소가 유효하다. 등록에 쓴 COM interface는 즉시 해제해
관측자가 객체 수명을 늘리지 않는다. interface가 없거나 마지막 해제 후 callback을 받지 못하면
검사를 실패시킨다. 장치 파괴 대기는 최대 5초다.

각 native cycle은 texture alias를 보유한 동안 callback 0, 마지막 alias를 해제하면 1인지 확인하고,
64×64 RT RGBA8/D32 depth/일반 RGBA8 texture 세 개를 256번 생성·해제한다. resource 파괴 수가
생성 수와 같아야 한다. device 해제 전후를 별도 관측하고 200ms 뒤 checkpoint를 추가한다.
장치 해제 뒤 private commit이 process 초기값보다 512MiB 넘게 증가하면 추가 cycle을 중단한다.
full-GBV 모드는 state tracking을 활성화한다. CPU debug layer 모드는 진단 대조다.

SDK 도구는 공개 API로 cycle당 8 batch × 8 frames를 실행한다. 매 batch의 pipeline/command와
입력 texture wrapper를 버린 뒤 fence 대기 및 color/depth 전체 pixel·복사 buffer 전체 byte를
비교한다. waitIdle 후 실제 SDK 보유 다섯 값이 모두 0이어야 한다. 다음에는 buffer와 alias를
보유한 상태로 Device를 버린다. 자원이 native generation을 계속 보유하다 마지막 alias 해제 시
native device 파괴 callback을 받는지 확인한다. native access는 같은 adapter device의 파괴 관측
등록에만 사용하며 렌더링 작업은 SDK가 수행한다.

Device가 없어진 뒤 `resourceStatistics()` 값은 **관측 불가**다. 해당 출력은 availability=false이며
다섯 필드 자체를 생략한다. native 파괴 callback을 SDK 통계 0으로 합성하지 않는다. SDK 정상
generation의 오류 수는 teardown 직전 unfiltered ERROR/CORRUPTION callback 집계로 확인한다.
native 도구도 자원 작업 구간의 unfiltered callback을 사용한다. unregister 뒤의 native 장치 파괴
메시지까지 관측했다고 주장하지 않는다.

## 실제 결과

Windows 11 build 26220, GTX 1660 SUPER와 WARP에서 각 8 cycles를 실행했다. OS/runtime 선택과
다른 build/driver의 결과를 혼동하지 않도록 adapter LUID/driver query 결과 및 로드된 DLL 경로를
기록했다. `d3d12.dll`, `D3D12Core.dll`, `D3D12SDKLayers.dll`, `d3d10warp.dll`의 SHA-256을 실행 전에
고정하고 실행 뒤 재확인했다. 로드한 모듈이 이 파일들에 없으면 runner가 실패한다.

| 새 process / 구성 | 자원 파괴 또는 전체 참조 | native 장치 파괴 |
|---|---:|---:|
| native hardware CPU debug | 6,152 / 6,152 resources | 8 |
| native hardware full GBV | 6,152 / 6,152 resources | 8 |
| native WARP full GBV | 6,152 / 6,152 resources | 8 |
| SDK hardware full GBV | 512 frames / 1,536 images / 128MiB buffer readback | 8 |
| SDK WARP full GBV | 512 frames / 1,536 images / 128MiB buffer readback | 8 |

총 native resource 파괴 callback 18,456회와 장치 파괴 callback 40회를 확인했다. SDK 총
1,024 frames / 3,072 전체 이미지 / 268,435,456 buffer bytes가 CPU 기준과 일치했다.
등록된 관측 구간의 예상하지 않은 ERROR/CORRUPTION은 모두 0이다.

마지막 cycle에서 resource 작업 후와 device 파괴·200ms 대기 후의 checkpoint 차이는 다음과 같다.
이 차이를 연속 peak, 순수 SDK 비용 또는 서로 다른 실행의 성능 비교로 사용하지 않는다.

| 구성 | private commit 감소 | DXGI nonlocal: 전 → 후 |
|---|---:|---:|
| native hardware CPU debug | 37.027MiB | 12,906,496 → 0 bytes |
| native hardware full GBV | 41.641MiB | 210,132,992 → 0 bytes |
| native WARP full GBV | 11.621MiB | 0 → 0 bytes |
| SDK hardware full GBV | 59.266MiB | 120,451,072 → 0 bytes |
| SDK WARP full GBV | 20.445MiB | 0 → 0 bytes |

WARP 관측의 0은 물리 VRAM 사용량 0의 증명이 아니다. DXGI query 상태와 null/숫자 계약을
그대로 기록한다. 실패한 hardware query는 거부하며 WARP 미지원 값은 명시적으로 남긴다.

native texture가 모두 파괴된 상태에서도 hardware GBV nonlocal 사용량이 남아 있었고 native
device 파괴 뒤 0으로 돌아왔다. SDK도 마지막 generation owner를 해제하면 native device가
파괴되고 해당 usage가 회수됐다. 이 결과는 **살아 있는 앱 texture 참조만으로 증가를 설명할 수
없음**과 장치 수명에 따른 회수를 보여준다. 정확한 runtime 내부 할당 원인이나 vendor 결함을
특정하지 않는다. SDK에 자동 장치 재생성을 우회책으로 추가하지 않았다.

CPU private commit은 초기값으로 돌아오지 않았다. 첫 cycle→마지막 cycle의 settled 값도
native hardware GBV 224.699→257.090MiB, SDK hardware GBV 146.926→177.246MiB였다.
따라서 반복 장치 해제만으로 무제한 메모리 안정성을 해결했다고 볼 수 없다. duration/plateau
policy를 새로 통과시킨 실행이 아니며 모든 결과는 `memory_acceptance_claim=false`다.

## 재현·회귀·보존

[도구 설명](../tools/memory_validation_probe/README.md)의 명령으로 같은 5개 process를 실행한다.
runner `lifetime.py`는 source/SDK/binary/runtime 입력을 고정하고 callback 수, alias 유지,
순서·완전한 reference·오류 범위·통계 availability를 엄격히 검사한다. 장치가 없는 상태의 가짜
SDK 0 통계, callback 누락/조기 파괴, 검증 축소, 잘못된 source/adapter, 부족한 전체 참조 등을
거부하는 Python 회귀 9개를 추가했다. 메모리 허용치를 바꾼 gate는 추가하지 않는다.

`Lifetime.cpp`와 전용 `native/CMakeLists.txt` 두 파일만 별도 폴더에 복사해 SDK header/library가
없는 상태에서도 빌드했다. 그 binary의 추가 2-cycle full-GBV 실행에서 98개 resource와 2개 device
파괴를 별도로 확인했다. 이 실행은 위 5종/40-cycle 합계와 구별한다. 최종 결과는 `final-campaign`에
있다. callback 저장소를 process 수명으로 보강하기 전 `campaign` 원본도 보존하지만 최종 소스의
새 실행으로 재분류하지 않는다.

이번 core CTest 22/22, D3D12-OFF 4/4, Python 91/91가 통과했다. 새 stress 감사는 compiler-read
24 inputs / 1 component이고 native 재현의 실제 linker 입력에 SDK library가 없음을 확인한다.
기존 SDK/common 감사 `--check`와 최신 SDK package 내용 감사도 통과했다. SDK source와 library는
변경하지 않았으므로 새 SDK ZIP을 만들지 않는다. 기존 6개 SDK ZIP·이전 source archive와 실패
gate는 그대로 보존한다. 공통 benchmark 72개/설치 consumer/이전 5분 재사용 검증은 같은 SDK의
기존 증거를 해시로 확인하며 이번에 새로 수행한 검사로 표현하지 않는다.

[원본](../benchmarks/reports/validation/2026-10-22-phase2-device-lifetime/)과
[최종 요약·증거](../benchmarks/reports/2026-10-22-phase2-device-lifetime/)에 재현 입력과 결과를 보존한다.
외부 라이브러리, competitor, Sandbox와 기존 GBV 설정은 바꾸지 않는다. 다른 host/runtime 대조,
원래 자원 신규 생성 장시간 GBV 증가 해결, interactive presentation, 예상치 못한 TDR/hot-unplug,
native Linux/macOS/Vulkan/Metal 및 remote CI는 여전히 남아 있다.
