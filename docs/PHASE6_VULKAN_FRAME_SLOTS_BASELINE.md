# P6-OPT-11 Vulkan Frame-in-flight slots 기준선

Windows x64 MSVC 19.44 Release에서 기존 Vulkan 설치 SDK를 사용했다.
한 장치의 생성 스레드와 그래픽 큐에서 슬롯당 RGBA8 렌더 타깃,
readback 버퍼, 공개 명령 목록 하나를 미리 준비한다. 모든 슬롯 구성에서
같은 257×129 프레임을 clear한 뒤 같은 제출에서 텍스처를 복사한다.
프레임마다 색을 바꾸며 fence가 완료된 슬롯에서 모든 RGBA 바이트를 비교한다.
측정 범위는 오프스크린 clear와 readback이며 픽셀 비교는 행의 RGBA 영역을 사용한다.

## 슬롯과 측정 구간

- 1·2·4개 슬롯을 차례로 채우고, 순환 시 해당 슬롯의 이전 fence와 참조를
  확인한 뒤 재사용한다. 마지막 슬롯들도 제출 순서로 회수한다.
  기록된 최대 outstanding 수는 CPU에서 제출하고 아직 회수하지 않은 슬롯 수다.
- 두 장치에서 슬롯 수마다 고정 Khronos validation으로 13프레임을 실행했다.
  6개 프로세스가 validation 오류 0, fence 순서와 마지막 SDK 소유량 0을 확인했다.
  별도 타이밍 프로세스에서도 매 프레임 참조와 종료 시 소유량을 확인했다.
- 장치별로 슬롯 순서를 바꾼 4개 블록을 실행했다. 각 구성은 매 블록에서
  새 프로세스로 13프레임을 워밍업하고 큐를 비운 뒤 121프레임을 측정했다.
  validation과 관측 계층을 끈 상태에서 기록, 제출, fence 대기,
  readback과 CPU 바이트 비교를 분리했다. 전체 시간은 121프레임의
  첫 기록부터 최종 슬롯 회수까지이며 초기 생성과 워밍업은 구간 밖이다.

## 두 GPU의 기준선

전체 시간은 프로세스 4개의 중앙값이다. 프레임별 구간은 프로세스별
중앙값을 먼저 구한 뒤 그 중앙값을 사용했다.

| GPU | 슬롯 | 전체 121프레임 (ms) | 기록 (µs) | 제출 (µs) | fence 대기 (µs) | readback·비교 (µs) | SDK 자원 (KiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GTX 1660 SUPER | 1 | 207.61 | 50.00 | 41.95 | 81.00 | 1081.10 | 433.25 |
| GTX 1660 SUPER | 2 | 163.65 | 40.45 | 29.10 | 1.10 | 1064.45 | 866.50 |
| GTX 1660 SUPER | 4 | 149.62 | 34.75 | 25.70 | 0.95 | 1039.20 | 1733.00 |
| Intel UHD 770 | 1 | 213.94 | 531.45 | 28.70 | 193.75 | 914.85 | 405.38 |
| Intel UHD 770 | 2 | 190.63 | 548.25 | 29.35 | 0.25 | 919.60 | 810.75 |
| Intel UHD 770 | 4 | 221.55 | 579.35 | 31.00 | 0.25 | 1093.45 | 1621.50 |

GTX에서 슬롯 2·4개 모두 네 블록에서 같은 블록의 1개 슬롯보다 전체
시간이 짧았다. Intel에서는 2개 슬롯이 네 블록 중 세 번, 4개 슬롯이
한 번 더 짧았다. 슬롯당 보유 자원량은 일정했다. 위 자원량은 설정 직후
SDK가 추적한 타깃과 readback 할당이며 드라이버의 총 VRAM이나 residency는
별도 계측 범위다.

30개 실행에서 전체 프레임 3,294개와 픽셀 436,823,928바이트를 실행 중 비교했다.
아카이브는 프로세스별 비교 결과, 장치·드라이버 식별 요약, 원시 CPU 시간과
슬롯 회수 카운터를 보관한다.
[요약](../benchmarks/reports/validation/2026-11-09-vulkan-frame-slots/summary.json) ·
[프로세스 결과](../benchmarks/reports/validation/2026-11-09-vulkan-frame-slots/campaign.zip)

## 후속 비교 계약

`P6-OPT-02`의 후보는 같은 슬롯 수, 이미지, 프레임 순서와 최종 drain으로
현재 경로와 비교한다. 후보의 명령 재사용이나 회수 정책을 평가할 때
동일 슬롯의 재기록 전 fence, 전체 readback, 사용 중 자원 소유와 종료 회수를
함께 확인한다. 후보 통합과 채택에는 Shader/PSO 수용(`P5-SHD-08`)이 선행한다.
GPU 실행 시간과 화면 출력에 대한 판단은 각각 별도 측정이 필요하다.

## 재현

저장소 루트에서 기존 Vulkan 헤더 입력과
[고정 validation 런타임](../tools/prepare_vulkan_validation.py)을 준비한다.
`<install>`은 Release 설치 SDK 경로이며 fingerprint와 source commit은
해당 설치 빌드의 값을 사용한다.

```powershell
cmake -S benchmarks/vulkan_frame_slots -B out/frame-slots-build -G "Visual Studio 17 2022" -A x64 "-DGFX_INSTALLED_SDK=<install>"
cmake --build out/frame-slots-build --config Release --parallel 1
python tools/run_vulkan_frame_slots.py run --binary out/frame-slots-build/Release/vulkan_frame_slots.exe --sdk "<fingerprint>" --sdk-source-commit "<commit>" --output out/frame-slots-results
python tools/run_vulkan_frame_slots.py check --input out/frame-slots-results
```
