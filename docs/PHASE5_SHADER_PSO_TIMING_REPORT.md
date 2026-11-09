# P5-SHD-08 셰이더 컴파일과 파이프라인 생성 측정

Release 설치 SDK의 기존 13×7 RGBA8 fixture를 사용한다. 같은 장치와 같은
`GraphicsPipelineDesc`에서 캐시 미사용, 비운 SDK 캐시의 첫 호출, 이어지는 적중을
비교한다. 한 세트를 워밍업으로 제외하고 열 세트를 기록한다. 홀수 번째 세트는
미사용 호출을 먼저, 짝수 번째 세트는 미스와 적중을 먼저 실행한다. 매 세트에서 미스와
적중의 반환 상태와 객체 동일성, 미사용 호출의 별도 객체 생성을 검사한다. 마지막 세트의
세 경로 모두 364바이트 참조 이미지를 그려야 한다. Metal은 256바이트 행 피치에 맞춘
패딩을 포함해 1792바이트를 대조한다.

파이프라인 시간은 `steady_clock`으로 잰 SDK 호출의 CPU 경과 시간이다. 캐시 비우기,
장치 생성, 검증용 draw·제출·대기·readback은 측정 범위 밖이다. 드라이버 내부 캐시
상태는 따로 제어하지 않으므로 SDK 캐시 미스에서도 드라이버가 작업을 재사용할 수 있다.
Vulkan 측정 프로세스는 동일 lavapipe ICD를 사용하며 라이브러리 로더 추적을 끈다.

## 컴파일 구간

- **DXIL과 SPIR-V.** 정점·프래그먼트별로 새 디스크 캐시에서 미스, 같은 키에서
  적중을 연속 측정한다. 두 상태 모두 전처리와 외부 검증을 수행한다. 미스에서만
  DXC 본 컴파일을 호출한다. 각 하위 프로세스와 전체 컴파일 경로의 경과 시간을 따로
  기록한다. 모든 반복의 셰이더 바이트 SHA-256이 일치하며 그 바이트를 파이프라인
  측정에 전달한다.
- **Metal.** 고정 SPIRV-Cross가 변환한 같은 MSL 2.1 소스로 Apple 오프라인 정점·
  프래그먼트 컴파일과 metallib 링크의 시간을 각각 기록한다. 오프라인 Metal 경로는
  SDK 디스크 캐시를 사용하지 않는다. Metal SDK는 MSL 소스를 받으므로 미사용 및 미스
  호출에는 런타임 MSL 라이브러리 컴파일도 포함된다.

컴파일 시간은 프로세스 시작과 종료를 포함한 CPU 경과 시간이다. 오프라인 도구 준비,
SDK 빌드와 SPIRV-Cross 변환은 측정 범위 밖이다. Windows WARP와 Linux lavapipe는
소프트웨어 렌더러를, macOS는 GitHub 호스팅 Metal 환경을 사용한다. 각 행의 수치는
해당 실행에서 경로를 비교하는 데 사용한다.

## 측정 결과

GitHub Actions에서 커밋 `46a6f8eb56a6c04b6c85bf4ec12165cc8ea1667f`의
설치 소비자를 실행했다. 각 열은 열 번 측정한 값의 중앙값이다. 순서별 미스는 각 다섯
번의 중앙값이다.

| 환경 | 캐시 미사용 (µs) | 미스 (µs) | 적중 (µs) | 미스 · 미사용 호출 먼저 (µs) | 미스 · 캐시 호출 먼저 (µs) |
|---|---:|---:|---:|---:|---:|
| Windows D3D12 WARP | 214.150 | 388.500 | 2.150 | 199.300 | 583.000 |
| Linux Vulkan lavapipe | 76.859 | 82.763 | 0.446 | 89.563 | 77.244 |
| macOS Metal ARM64 | 263.916 | 197.104 | 0.625 | 200.208 | 182.417 |
| macOS Metal Intel | 471.202 | 569.614 | 1.373 | 484.749 | 585.543 |

WARP의 미스 중앙값은 호출 순서에 따라 199.300 µs와 583.000 µs였다. 순서별
표본 수가 같으며 전체 중앙값에는 두 순서가 모두 반영된다.

| DXC 대상 | 정점 미스 (ms) | 정점 적중 (ms) | 프래그먼트 미스 (ms) | 프래그먼트 적중 (ms) |
|---|---:|---:|---:|---:|
| Windows DXIL | 46.24 | 26.88 | 45.19 | 26.82 |
| Linux SPIR-V | 9.85 | 5.03 | 9.26 | 5.00 |

| Apple 오프라인 도구 | 정점 컴파일 (ms) | 프래그먼트 컴파일 (ms) | 링크 (ms) |
|---|---:|---:|---:|
| macOS ARM64 | 35.93 | 32.89 | 26.51 |
| macOS Intel | 83.86 | 82.18 | 68.42 |

[반복 측정 원자료](../benchmarks/reports/validation/2026-11-09-shader-pso/measurements.json) ·
[Windows CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42546862531) ·
[Linux CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42546862694) ·
[macOS CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42546862368)

## 재현

고정 도구와 빌드 전제 조건은 기존 [DXIL](PHASE5_D3D12_DXIL_REPORT.md),
[Vulkan SPIR-V](PHASE5_VULKAN_SPIRV_REPORT.md), [Metal](PHASE5_METAL_SPIRV_REPORT.md)
보고서에 있다. 각 명령은 새 출력 디렉터리를 사용한다.

```text
python tools/d3d12_dxil.py run --measure --output out/shader-pso-dxil
python3 tools/vulkan_spirv.py run --measure --output out/shader-pso-spirv --icd /usr/share/vulkan/icd.d/lvp_icd.json
python3 tools/metal_spirv.py run --measure --output out/shader-pso-metal
```

동일 스크립트의 `check --output <경로>`는 receipt의 파일 해시, 반복 수, 컴파일러
호출과 캐시 상태, 셰이더 객체 해시, 세 경로의 전체 readback, 중앙값을 다시 검사한다.
`timing/compiler.json`에는 컴파일 원자료가, `timing/summary.json`에는 중앙값과
호출 순서대로 정렬된 파이프라인 원자료가 있다.
