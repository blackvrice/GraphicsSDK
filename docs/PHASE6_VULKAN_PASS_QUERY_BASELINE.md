# P6-OPT-14 Vulkan Query pool·render pass 기준선

Windows x64 MSVC Release에서 기존 Vulkan 설치 SDK를 사용했다. 명령
목록 하나에 같은 33×9 RGBA8 타깃의 clear pass를 1회 또는 16회 기록하고,
각 pass 직후 별도 readback으로 복사한다. 기록과 pass마다 색을 바꾸고
모든 RGBA 바이트를 비교한다. 타깃과 readback은 구성마다 미리 만든다.

| 프로필 | 차이 |
| --- | --- |
| Plain | 공개 타임스탬프를 요청하지 않고 동일한 clear와 copy를 실행한다. |
| Timestamp | 첫 clear 전과 마지막 copy 뒤 타임스탬프를 기록하고 fence 완료 뒤 결과를 읽는다. |

## 실행 계약

- 고정 core·sync validation과 native 관측을 별도 프로세스에서 실행한다.
  두 모드 모두 미제출 목록 폐기, 제출 전 결과 거부, timestamp 프로필의
  완료 후 결과와 반복 조회를 확인한다. 관측에서는 각 객체의 생성과 반환,
  query reset/write/read, framebuffer가 연결된 render pass보다 먼저
  반환되는 순서를 검증한다.
- 제출 프레임은 fence 완료 뒤까지 명령 핸들을 보유한다. 결과를 읽은 뒤
  핸들을 내려놓고 query pool·render pass·framebuffer가 모두 반환되는지
  확인한다. Plain은 완료 후에도 타임스탬프 조회를 거부한다.
- 타이밍은 두 계층을 끄고 구성마다 독립 프로세스 네 번을 실행한다.
  프로필 순서를 번갈아 바꾸며 워밍업 4회 후 16개 표본을 보존한다.
  명령 `begin`, `beginRenderPass` 합, 전체 기록, fence 대기, query 조회,
  명령 해제를 분리한다. 처음 두 구간은 전체 기록에 포함된다.
  GPU 간격은 timestamp 프로필의 전체 clear·copy 구간에 한정한다.

## 결과

GTX 1660 SUPER와 Intel UHD 770에서 48개 프로세스를 실행했다.
5,848개 이미지의 6,947,424바이트와 GPU 타임스탬프 간격 344개를 검증했다.
고정 validation 8건에서 오류는 0이었다.

native 관측 8건에서 명령 기록마다 query pool을 하나 생성하고 초기화했다.
Plain에서도 같은 할당이 일어났으며 write/read 호출은 0이었다.
Timestamp는 제출마다 두 timestamp를 기록하고 완료 뒤 native 결과를 한 번
읽었다. 같은 타깃을 재사용한 구성에서도 각 pass마다 render pass와
framebuffer를 생성했다. 미제출 대조군을 포함한 query pool 32개,
render pass 212개, framebuffer 212개가 모두 반환됐다.

아래는 프로세스별 중앙값을 구한 뒤 네 프로세스의 중앙값을 구한
CPU 시간(µs)이다. GPU 간격과 나머지 CPU 구간은 원자료에 따로 기록했다.

| GPU | pass | 프로필 | 명령 begin | pass begin 합 | 전체 기록 | query 읽기 | 명령 해제 |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| GTX 1660 SUPER | 1 | Plain | 10.02 | 6.70 | 26.05 | — | 8.28 |
| GTX 1660 SUPER | 1 | Timestamp | 8.93 | 5.40 | 22.05 | 0.70 | 6.98 |
| GTX 1660 SUPER | 16 | Plain | 15.15 | 48.90 | 127.88 | — | 15.48 |
| GTX 1660 SUPER | 16 | Timestamp | 13.95 | 48.02 | 123.32 | 1.12 | 13.98 |
| Intel UHD 770 | 1 | Plain | 271.70 | 71.28 | 362.78 | — | 244.28 |
| Intel UHD 770 | 1 | Timestamp | 316.68 | 79.50 | 415.40 | 1.18 | 266.20 |
| Intel UHD 770 | 16 | Plain | 306.27 | 133.22 | 654.88 | — | 371.82 |
| Intel UHD 770 | 16 | Timestamp | 281.38 | 137.30 | 622.62 | 1.20 | 341.02 |

명령 begin에는 command pool·buffer 준비와 query 준비가 함께 포함된다.
query pool 할당만의 CPU 비용은 후보와 동일 조건에서 분리 측정할 항목이다.
[측정 요약](../benchmarks/reports/validation/2026-11-09-vulkan-pass-query/summary.json) ·
[프로세스별 원자료](../benchmarks/reports/validation/2026-11-09-vulkan-pass-query/campaign.zip)

## 후속 후보 계약

`P6-OPT-05`의 query 후보는 미사용 기록의 할당 생략 또는 pool 재사용을
각각 비교한다. 아직 완료되지 않은 query의 덮어쓰기 방지, 완료 뒤 결과
조회·반복 조회, 결과를 읽기 전 핸들 보유, 미제출 폐기와 퇴역을 확인한다.
render pass와 framebuffer 후보는 같은 형식·load/store 조건과 타깃으로
비교한다. 형식·크기·attachment view 변경을 분리해 전체 픽셀과 반환 순서를
검증한다. 이 기준선은 단일 컬러 clear·copy 경로를 다룬다.
후보 구현에는 Shader/PSO 수용(`P5-SHD-08`)이 선행한다.

## 재현

저장소 루트에서 Vulkan 헤더와
[고정 validation 런타임](../tools/prepare_vulkan_validation.py)을 준비한다.
관측 DLL과 manifest는 벤치마크와 함께 빌드한다.
`<install>`, fingerprint와 source commit은 비교할 Release 설치 SDK의 값이다.

```powershell
cmake -S benchmarks/vulkan_pass_query_baseline -B out/pass-query-build -G "Visual Studio 17 2022" -A x64 "-DGFX_INSTALLED_SDK=<install>"
cmake --build out/pass-query-build --config Release --parallel 1
python tools/run_vulkan_pass_query_baseline.py run --binary out/pass-query-build/Release/vulkan_pass_query_baseline.exe --sdk "<fingerprint>" --sdk-source-commit "<commit>" --output out/pass-query-results
python tools/run_vulkan_pass_query_baseline.py check --input out/pass-query-results
```
