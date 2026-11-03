# macOS Metal 장치·offline 도구 가용성

2026-11-03. P4-MTL-01의 환경 준비 근거다. 공통 Core CPU 검증 뒤 별도 native
Objective-C++ 프로그램으로 Metal 장치 노출과 offline MSL 도구를 확인한다.
SDK Metal backend 구현·GPU command 제출·화면·성능 수용은 이 단계에 포함하지 않는다.

`MTLCopyAllDevices`와 `MTLCreateSystemDefaultDevice`로 목록/default를 관측한다.
registry ID는 정밀도를 잃지 않는 16자리 hex 문자열로 보관하며 중복·빈 이름·잘못된
크기/boolean과 목록에 없는 default를 거부한다. 각 장치가 있으면 ID를 지정한 별도
프로세스로 다시 선택하고 목록 불변을 확인한다. 없는 ID와 잘못된 형식은 종료 코드 1,
빈 stdout 및 지정한 오류로 거부한다. 장치가 0개면 부재를 기록하며 GPU 성공으로
승격하지 않는다. native API 장치 노출은 물리 GPU나 SDK backend 수용의 증거가 아니다.

실제 로드한 Metal/Foundation framework 경로를 dyld 목록에서 확인한다. 장치의 name,
registry ID, low-power/removable/headless/unified-memory, buffer/threadgroup 한계를
기록한다. 이 값은 GPU별 descriptor이며 렌더링·queue·submission 동작 검증이 아니다.
registry ID의 범위는 해당 OS 환경이며 다른 CI VM/부팅에 영속한 ID라고 가정하지 않는다.
[Apple 장치 목록 API](https://developer.apple.com/documentation/metal/mtlcopyalldevices%28%29/),
[registry ID 설명](https://developer.apple.com/documentation/metal/mtldevice/registryid?language=objc)

`xcrun --sdk macosx`의 metal/metallib 경로를 찾고, 최소 kernel을 AIR와 metallib로
컴파일한다. 잘못된 MSL은 compiler error로 거부하고 출력 binary가 없어야 한다.
도구 검색 실패나 명시적인 missing Metal Toolchain 오류만 unavailable로 기록한다.
일반 shader 오류·compiler crash·권한 오류를 도구 부재로 처리하지 않는다. 생성 파일은
크기/hash로 보관하고 GPU에 로드하거나 실행하지 않는다.
[Apple offline shader library 절차](https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files)

```sh
python3 tools/macos_metal_probe.py run --core out/macos-core-evidence --output out/macos-metal-evidence
python3 tools/check_python_contracts.py --output out/macos-python
python3 tools/macos_metal_probe.py check --output out/macos-metal-evidence --python out/macos-python --live
```

동일 source의 완료된 Core receipt, native Darwin/architecture와 fresh output이 필요하다.
Core와 같은 compiler/architecture/SDK로 단일 Objective-C++ unit을 빌드하며 ARC와 O2를
사용한다. source ZIP·원본 stdout/stderr·command PID/유한 직렬 시간·cache/graph·lipo·
실행 파일/생성 shader hash를 보관하고 source/build 불변을 확인한다. 전체 system
header/linker 입력이나 라이선스 감사는 수행하지 않으며 외부 SDK/native binary를
재배포하지 않는다. Python receipt로 실제 검사한 probe runner 코드와 source를 결합한다.

8개 CPU 계약은 장치 0개·여러 장치·정확한 ID·default/중복/형식/limit·framework·
boolean·GPU 수용 승격 변조, 정확한 부정 대조와 알려진 도구 부재, host/architecture
불일치를 검사한다. 합성 테스트는 Metal GPU 실행 근거가 아니다.

CI는 기존 일반 macOS arm64/Intel matrix를 사용하며 GPU larger runner를 생성하지 않는다.
Core CPU 단계의 GPU 미조회 범위와 별도 Metal availability 단계를 구분한다. 실제
SDK Metal 장치/queue/backend·버퍼/텍스처/draw·표시·native validation은 이후 개발과
GPU 실행이 필요하다. P3-LNX-15 및 Phase 3 gate 상태는 변경하지 않는다.


## 실제 실행과 보관 증거

소스 `611f5c858b2596851e689233bebc4d1ebe4bbee4`의
[macOS arm64/Intel CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41588150132)가
모두 통과했다. 각 환경에서 `Apple Paravirtual device` 1개, default와 같은 registry
ID, 정확한 ID 재선택과 두 잘못된 ID 거부를 확인했다. arm64의 unified memory는 true,
최대 buffer 길이는 3,758,096,384바이트였고 Intel은 false·1,073,741,824바이트였다.
이 descriptor 차이를 그대로 보존하며 물리 GPU 또는 동일 성능 조건이라고 주장하지 않는다.
registry ID는 해당 CI VM에서 관측한 값이며 다음 실행에 고정 ID로 재사용하지 않는다.

두 환경 모두 Metal/Foundation framework 실제 로드를 관측했고, offline MSL→AIR→
metallib 생성과 invalid.metal의 실제 unknown-type/expected-brace compiler 진단·
종료 코드 1·출력 binary 없음이 확인됐다. 단일 Objective-C++ compile unit과 source
입력 7개를 보관했다. GPU command/queue/dispatch는 실행하지 않았고 SDK Metal backend는
사용하지 않았다. Native API 장치와 offline 도구가 가용한 후보 환경을 확보한 결과다.

각 환경의 기존 Core CTest 11개·설치 소비자·전체 Python 계약 1,473개도 통과했다.
[동일 소스 Windows/Linux 회귀 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41588150153)도
통과했다. 두 Mac 결과를 `benchmarks/reports/validation/2026-11-03-metal-availability/`에
원본 152개·source ZIP·inventory/manifest/summary와 함께 보관했고 Windows에서 독립
재생했다. 외부 SDK·native executable·AIR/metallib binary는 재배포하지 않으며 해당
파일의 hash/크기와 원본 compiler 출력만 보관한다. 자체 hash 목록이고 외부 신뢰
서명은 아니다. archived source/native code는 실행하지 않는다.

```sh
python3 benchmarks/reports/validation/2026-11-03-metal-availability/verify.py
```

Windows/Linux/macOS CI에 이 재생을 추가한다. 로드맵 143개 상태와 active Phase 3를
유지하며 P4-MTL-01의 환경 준비 근거에 추가했다. 다음 단계는 이 환경에서 C++ 공개
API와 Metal 장치/queue/submission 구현을 연결하고 실제 command 완료·오류 계약을
검증하는 것이다. 실제 buffer/texture/draw·표시·물리 GPU/성능 수용은 별도 남아 있다.
