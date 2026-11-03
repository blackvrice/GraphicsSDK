# Metal SDK 장치·queue·submission 준비

이 문서는 아래 소스 `8ad0932`의 submission 전용 구현과 보관 근거다. 후속 buffer·동기화·
수명 구현과 native 자원 퇴역 방식은 [Metal 버퍼 보고서](PHASE4_METAL_BUFFER_REPORT.md)를 따른다.

P4-MTL-02의 구현 준비다. `GFX_ENABLE_METAL=ON`은 native macOS에서만 지원하며 기본값은
OFF다. C++ 공개 header에는 Objective-C를 노출하지 않고 private `Metal.cpp`만 CMake의
OBJCXX 언어·C++23·ARC로 컴파일한다. 기존 SDK source fingerprint에 이 unit이 포함된다.
정적 `GraphicsSDK::Core` 패키지는 system Metal/Foundation framework 의존성을 전달한다.

macOS에서 Xcode C++23 toolchain·CMake·Ninja를 준비한 뒤 실행한다. 장치가 없으면 이
Metal 계약 검사는 실패하며 CPU-only 검사는 `GFX_ENABLE_METAL=OFF`를 사용한다.

```sh
cmake -S . -B out/metal -G Ninja -DCMAKE_BUILD_TYPE=Release -DGFX_ENABLE_D3D12=OFF -DGFX_ENABLE_VULKAN=OFF -DGFX_ENABLE_METAL=ON -DGFX_BUILD_EXAMPLES=OFF
cmake --build out/metal --parallel 3
ctest --test-dir out/metal --output-on-failure --no-tests=error
cmake --install out/metal --prefix out/metal-install
cmake -S tests/macos_metal_consumer -B out/metal-consumer -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/out/metal-install"
cmake --build out/metal-consumer
out/metal-consumer/macos_metal_consumer
out/metal-consumer/macos_metal_buffer_consumer
```

## 구현 계약

- MTLCopyAllDevices의 registry ID를 backend-scoped AdapterId로 정렬·중복 검사한다.
  default는 MTLCreateSystemDefaultDevice이며 명시적 ID는 정확히 일치해야 한다. 부재 시
  다른 장치로 fallback하지 않는다. ID는 해당 OS 실행 범위이며 영구 hardware serial이 아니다.
- Metal에는 이 API의 software/virtual 판별과 PCI 메모리 총량이 없어 이를 추정하지 않는다.
  softwareAdapter=false 선택만 지원하며 이것으로 물리 GPU라고 판정하지 않는다.
  validation·software·device-loss-testing 요청은 Unsupported로 거부한다.
- 장치별 native queue와 단일 제출 command buffer, begin/end/재기록·중복 제출 거부,
  blit encoder의 UTF-8 debug signpost를 지원한다. 객체 파괴 후에도 native 제출을 보유한다.
- fence는 장치 세대와 단조 증가 값으로 검사한다. 다른 세대·0·미제출 값·범위 밖 timeout을
  거부한다. 0ms는 native status polling만 수행하며 finite wait는 최대 1ms 간격의 CPU
  polling을 사용한다. timeout은 제출을 취소하지 않고 동일 fence로 재시도할 수 있다.
  D3D12/Vulkan/Metal은 연결된 SDK 내 공통 atomic 번호 발급기를 사용해 backend 사이의
  fence owner 충돌을 방지한다. 번호 고갈은 wrap/reuse 없이 실패하며 CPU 계약은 고갈과
  4개 스레드의 256개 번호 발급을 검사한다.
- native Completed만 완료 값으로 반영하고 완료 제출을 순서대로 해제한다. Error는 원본
  NSError code/설명을 보존하고 deviceRemoved/accessRevoked를 DeviceLost, 나머지를
  BackendFailure로 고정한다. 이후 성공으로 덮어쓰지 않는다. 장치 파괴 후 자식 native
  조작은 DeviceLost이며 마지막 소유자는 남은 native command completion까지 기다린다.
- 생성 스레드 외의 가변 native 조작을 InvalidState로 거부한다. resourceStatistics는
  완료 제출을 정리하고 pending 수를 반환한다. 이 단계에는 공개 resource allocation이 없다.
- buffer/texture/pipeline/draw·GPU timestamp·표시·recovery·native validation·OS memory
  budget은 Unsupported다. caps는 이 기능들을 false로 유지한다.

Metal command buffer [commit](https://developer.apple.com/documentation/metal/mtlcommandbuffer/commit%28%29)은
native queue 제출이며 [error status](https://developer.apple.com/documentation/metal/mtlcommandbufferstatus/error)는
NSError를 조사해야 한다. 기존 command buffer API를 사용하며 Metal 4 이식은 범위 밖이다.

## 검증과 증거

`MetalSubmissionTests.cpp`는 실제 장치 없이 skip하지 않는다. 열거한 모든 ID의 재선택,
부재·다른 backend·미지원 policy, 기록 순서·잘못된 marker·중복 제출, foreign command/
fence·미제출 fence·유효 timeout, zero poll 결과와 재대기, 66회 순차 native 완료,
명령 객체 즉시 파괴·재사용, 완료 후 보유 해제, 7종 잘못된 스레드 호출, 장치 파괴 후
자식 거부를 검사한다. 추가 1회는 device 파괴 직전 제출해 마지막 소유자 정리를 실행한다.
Native 오류를 의도적으로 주입하거나 timeout 발생을 강제하지 않는다. zero poll은 실제
success/Timeout 중 관측값을 기록한다. 비어 있거나 marker만 있는 command 제출이며
데이터 전송·shader dispatch·draw 정확성, 물리 GPU·성능·native validation 수용이 아니다.

macOS arm64/Intel CI는 CPU-only Core와 availability를 분리한 다음 Metal ON 빌드에서
CPU CTest 11개+Metal CTest 1개, 직접 실행, 설치 public SDK 소비자를 수행한다. 총 native
계약 실행은 3회·제출 201회다. 각 실행에서 native 완료를 확인하는 제출은 66회이고
장치 파괴 시 정리 제출은 1회다. 전체 Python suite와 같은 source/fingerprint,
architecture/SDK, source ZIP, compiler graph, cache, 원본 stdout/stderr, PID/직렬 시간·
설치 header·산출물 hash를 기록한다. binary나 외부 SDK는 보관하지 않는다.

```sh
python3 tools/macos_metal_backend.py run --availability out/macos-metal-evidence --output out/macos-metal-backend-evidence
python3 tools/macos_metal_backend.py check --output out/macos-metal-backend-evidence --python out/macos-python --live
```

Offline check는 archived source/native code를 실행하지 않는다. 7개 CPU receipt 계약은
실행 누락·skip·scope 승격·잘못된 compiler graph/architecture/language를 거부한다.
이는 native 실행을 대체하지 않는다.

Phase 3 gate·P3-LNX-15·P4-MTL-01 의존성과 물리 GPU 수용은 미완료다. 143개 roadmap
ID/status와 active Phase 3를 유지하고 P4-MTL-02의 진행 근거만 추가한다.

## 실제 실행과 보관 결과

소스 `8ad09321d1bfe284f704e92a7c76266b8c3b9c94`, SDK fingerprint
`e20fb799357fa64ca59d9c474310b9396e52d70b3c17a409f36668d83559461d`의
[macOS arm64/Intel CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41611111609)가
모두 통과했다. 두 환경 모두 Apple Paravirtual device 1개를 관측했고 registry ID 정확한
선택과 부재 거부, native command queue·marker·기록/제출/완료·zero poll Timeout 후
재대기·자식 수명 계약을 실행했다. ID는 각 VM 실행의 관측값으로 다음 실행에 고정하지 않는다.
물리 GPU 판정 또는 데이터 전송/draw 검증으로 사용하지 않는다.

각 환경에서 native CTest 12개(기존 CPU 11개+Metal 1개), 직접 실행과 설치 소비자가
통과했다. 직접/설치 실행은 각각 116개 assertion을 수행했다. 제출은 실행당 66회
Completed 확인과 1회 장치 파괴 시 native 완료 대기·정리로 총 201회다. 두 zero poll은
실제로 Timeout을 반환했고 같은 fence 재대기가 성공했다. timeout 강제 유도나 native
NSError 주입은 하지 않았다. 공통 세대 번호 고갈·4개 스레드 256개 발급을 포함한
어댑터 CPU 실행은 262개 assertion이 통과했다.

각 환경의 전체 Python 1,480개도 통과했다. 같은 소스의
[Windows/Linux 회귀](https://github.com/blackvrice/GraphicsSDK/actions/runs/41611111298)와
[Linux Vulkan 컴파일](https://github.com/blackvrice/GraphicsSDK/actions/runs/41611111329)도 통과했다.
SDK/test compiler graph 18 units(그중 private ARC Objective-C++ 1개), 설치 소비자 1 unit,
public header 13개와 이번 helper source 7개를 기록했다. SDK unit은 strict C++23,
CMake test target의 GNU C++23도 허용하고 architecture/SDK/O2/잘못된 backend는 계속 검사한다.

원본 216개를 `benchmarks/reports/validation/2026-11-03-metal-submission/`에 source ZIP,
inventory/summary/manifest와 보관했고 Windows에서 현재 검사기로 독립 재생했다.
evidence ZIP은 1,011,490바이트다. system SDK/framework·native binary를 재배포하지 않으며
해당 binary 크기/hash와 compiler/log/receipt를 보존한다. manifest는 자체 hash 목록이며
외부 신뢰 서명이 아니다. 과거 Metal availability·Core·Linux 증거도 그대로 유지한다.

```sh
python3 benchmarks/reports/validation/2026-11-03-metal-submission/verify.py
```

Windows/Linux/macOS CI에 이 재생을 연결한다. P4-MTL-02는 선행 Phase 3/P4-MTL-01 수용과
native fault/validation·물리 GPU 검증을 완료한 것으로 승격하지 않는다. 다음 구현은
P4-MTL-03의 buffers·전송 동기화·resource lifetime이며 별도 실제 payload 검사가 필요하다.
