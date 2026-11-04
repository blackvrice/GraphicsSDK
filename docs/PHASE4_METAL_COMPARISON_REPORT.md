# P4-MTL-08 Metal 경쟁 SDK 비교

각 macOS runner에서 GraphicsSDK, SDL3 GPU, LLGL이 같은 장치를 선택했는지 확인하고
같은 Metal draw 작업을 독립 프로세스에서 실행한다. 각 실행 환경에서 나열된 Metal 어댑터가
하나이며 시스템 기본 장치와 일치해야 한다. GraphicsSDK는 그 registry ID로 장치를 명시한다.
LLGL은 공개 Metal handle로 장치를 받고 생성 후 ID를 다시 읽는다. SDL3는 고정 소스가 시스템
기본 장치를 선택하는 조건에서 `metal` 드라이버와 실제 장치 이름을 확인한다. SDL의 ID는
이 선택 경로와 단일 장치 조건으로 결합한다.

| 입력 | 고정 커밋 | 라이선스 |
|---|---|---|
| SDL3 | `3b2bc951500ca6a1f428af77e036e732d9a8afe3` | Zlib |
| LLGL | `649c0cc8fa608312c5e4b94dae409a5859fc7553` | BSD-3-Clause |

두 입력은 기존 비교에 사용한 고정 원본이며 비교 실행기에만 링크한다. CI는 체크아웃 커밋과
라이선스 원문 SHA-256을 확인한다. SDL은 Cocoa에서 Metal 장치를 초기화하며 창을 만들지 않는다.
Mac2를 지원하지 않고 Mac1을 지원하는 장치에는 SDL의 공개 Mac1 호환 옵션을 사용한다.
LLGL은 Metal 정적 모듈을 빌드한다. 각 실행 파일은 동일 아키텍처의 Release `-O2 -DNDEBUG`로
빌드하고 SDK는 새로 설치한 패키지에 연결한다.
[SDL 장치 생성 옵션](https://wiki.libsdl.org/SDL3/SDL_CreateGPUDeviceWithProperties),
[SDL GPU 소스](https://github.com/libsdl-org/SDL/tree/3b2bc951500ca6a1f428af77e036e732d9a8afe3/src/gpu/metal),
[LLGL Metal 소스](https://github.com/LukasBanana/LLGL/tree/649c0cc8fa608312c5e4b94dae409a5859fc7553/sources/Renderer/Metal)

작업은 64×32 RGBA8 단일 타깃에서 파란색을 clear한 뒤 19×11 scissor 안에 동일한 MSL의
빨간 삼각형을 그리는 것이다. 프레임당 1·128·1,024 draw를 각각 실행한다. 검증 프로세스는
Metal API Validation을 켜고 모든 픽셀을 CPU 기준과 비교한다. 세 SDK가 검증을 통과해야 측정을 시작한다.

측정 프로세스는 검증을 끄고 입력별 4회 예열 후 12개 원본 샘플을 남긴다. 기록·제출·큐 완료
대기 구간을 CPU 단조 시계로 분리하며 마지막 프레임의 readback은 측정 밖에서 비교한다.
기록과 제출의 합은 각 SDK가 실제 네이티브 인코딩을 수행하는 시점이 다를 수 있다는 점을 반영한다.
큐 대기는 호스트에서 관찰한 시간이다. 타깃 메모리 모드도 입력별로 보존한다.

이 workflow는 가상 Metal 장치에서 재현 가능한 진단을 수행한다. 성능 수용은 실제 Mac에서
동일한 장치, 작업과 검증 기준을 사용해 별도로 진행한다.

## 재현

GitHub Actions의 `macos-metal-comparison.yml`은 Apple Silicon과 Intel을 각각 실행한다. macOS에서
직접 실행할 때는 `.deps/SDL`과 `.deps/LLGL`을 위 커밋으로 준비한 뒤, CMake·Ninja·Xcode 도구가
있는 깨끗한 SDK 체크아웃에서 아래 명령을 실행한다.

```sh
python3 tools/macos_metal_comparison.py run --output out/macos-metal-comparison
```

다른 OS에서는 CI 아티팩트의 원본 이미지, 타이밍 CSV, 컴파일 설정과 라이선스 원문을 재검증한다.

```sh
python3 tools/macos_metal_comparison.py check --output out/macos-metal-comparison
```

## 2026-11-04 실행 결과

소스 `1ca2c6d`를 macOS ARM64와 Intel에서 각각 빌드했다. 각 runner에서 세 SDK의 장치가
같은 `Apple Paravirtual device`로 결합됐다. ARM은 Mac2 경로를 사용했고, Intel은 Mac1 지원을
확인한 뒤 SDL의 공개 호환 옵션을 적용했다. LLGL 타깃은 `managed`, GraphicsSDK와 SDL3 타깃은
`private`이었다. [비교 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41785299697)

아키텍처마다 전체 RGBA 이미지 18개(147,456바이트)가 기준과 일치했고, CPU 시간 원본 108개를
기록했다. 다음은 프레임당 1,024 draw의 구간별 중앙값이다. 단위는 ms다.

| 장치 환경 | SDK | CPU 기록+제출 | 호스트 큐 대기 |
|---|---|---:|---:|
| ARM64 가상 Metal | GraphicsSDK | 35.043 | 2.133 |
| ARM64 가상 Metal | SDL3 | 0.091 | 1.085 |
| ARM64 가상 Metal | LLGL | 0.171 | 1.285 |
| Intel 가상 Metal | GraphicsSDK | 106.843 | 1.358 |
| Intel 가상 Metal | SDL3 | 0.152 | 0.879 |
| Intel 가상 Metal | LLGL | 0.138 | 0.857 |

GraphicsSDK의 큰 draw 입력 비용은 주로 CPU 기록에서 관찰됐다. `recordDraw`는 draw마다
`draws`와 `order`에 `reserve(size + 1)`을 호출한다. 이 호출은 벡터 재할당과 기존 draw 복사의
원인 후보다. 원본 CSV에는 기록과 제출 시간이 구분돼 있다. 서로 다른 타깃 메모리 모드와 가상 runner의 시간을 포함한
진단값이며, 실제 Mac의 GPU 시간이나 성능 수용은 별도로 검증한다.
[Metal 기록 코드](../src/backends/metal/Metal.cpp)

원본 아카이브에는 픽셀, CSV, 장치 정보, 컴파일 그래프, 프로세스 환경, 고정 라이선스 원문과
SDL의 HIDAPI·YUV 변환 고지를 담았다. 비교 실행 파일과 외부 SDK 바이너리는 CI 빌드에만 사용했다.
현재 검사기가 Windows·Linux·macOS에서 원본을 재검증한다.
[원본 목록과 구간별 결과](../benchmarks/reports/validation/2026-11-04-metal-comparison/summary.json)

```sh
python3 benchmarks/reports/validation/2026-11-04-metal-comparison/verify.py
```
