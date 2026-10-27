# P3-BEN-01 — 현재 제품용 Vulkan 비교 계약·새 build roots

2026-10-27 Windows x64 Release에서 현재 GraphicsSDK 설치 라이브러리, 고정 SDL static
라이브러리와 upload/draw/PSO/allocation 소비자를 각각 새 폴더에 빌드했다. **P3-BEN-01은
완료**이며, 정확성·통계 수용 `P3-BEN-02`와 workload별 재측정은 미완료다.

## 고정한 입력

| 입력 | 실제 확인 범위 |
|---|---|
| 소스 커밋 | `6cc848ac9e34273ba19bacd60909da03b72e9672` |
| 제품 SDK | 28파일, Windows fingerprint `197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`. 직전 제품 소스와 byte 일치 |
| 빌드 | Visual Studio 17 2022 / x64 / Release / `/O2` / `/MD` / IPO OFF. 실제 compiler `MSVC 194435228` |
| SDK 구성 | D3D12 ON, Vulkan ON, XCB OFF, tests/examples OFF. 설치 Core와 빌드 Core의 SHA256 일치, 설치 public headers 일치 |
| SDL | `3b2bc951500ca6a1f428af77e036e732d9a8afe3`, static/GPU/Vulkan ON, shared/tests/test-library OFF. SDK와 같은 MSVC runtime |
| SDK Vulkan-Headers | `b379292b2ab6df5771ba9870d53cf8b2c9295daf`. SDL 자체 포함 헤더는 SDL pin 소속으로 별도 기록 |
| GPU·driver | NVIDIA GeForce GTX 1660 SUPER / Windows driver `32.0.16.2101`; vendor `4318`, device `8644` |
| 장치 식별 | UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, LUID `907c010000000000` |
| 실제 새 roots | `out/phase3-comparison-20261027-3/`: sdk-build, sdk-install, sdl-build, consumers/{upload,draw,pso,allocation} |

소비자의 실제 linker-read 기록에서 모두 이 새 Core.lib·SDL3-static.lib를 읽었음을 확인했다.
각 실행 파일의 제품·소비자 fingerprint도 현재 소스와 일치한다. raw receipt에는 각 source,
생성 입력, compiler/linker-read 파일, 시스템 헤더·라이브러리, CMake executable, loader와
산출물의 SHA256을 보존했다. 이 보수적인 입력 목록은 native symbol 도달성이나 추가
라이선스 승인 증명이 아니다. SDK의 공개 API·제품 구현은 바꾸지 않았다.

## 비교 계약과 확인 결과

[계약 JSON](../benchmarks/phase3_comparison/contract.json)은 기존 13개 workload를 명시한다.

| 프로필 | 고정 workload | 향후 시간의 의미·한계 |
|---|---|---|
| upload | 65×37 RGBA8, 1080p RGBA8, 4K RGBA8, 4K RGBA16F | CPU payload write부터 upload fence 완료까지. readback은 밖. SDK padded staging과 SDL tight layout 차이 유지 |
| draw | draw1 / draw1000 / instanced1000; 65×37 RGBA8, scissor [5,3,55,31] | record/submit/wait 및 완료 wall time. changing full-image readback은 밖 |
| PSO | 1 / 8 pipelines, 65×37 RGBA8 | public create/release CPU API. GPU 사용·full readback은 밖. native 생성 수·cache·물리 회수는 미검증 |
| allocation | buffer1/1000 (4096 bytes), texture1/1000 (65×37 RGBA8) | public create/release CPU API. 사용·changing full readback은 밖. SDK general device buffer와 SDL vertex usage 차이 유지 |

이 단계에서는 장치 생성·식별만 실행했다. 프로필×SDK **8개 probe가 동일 UUID·driver UUID·
LUID·환경**을 반환했고, warmup/sample/검증 bytes·자원·제출 수는 모두 0, samples 배열은 비어 있다.
8개의 없는 GPU 이름 control도 종료 코드 1, 빈 stdout와 정확한 장치 선택 오류를 반환했다.
probe에 선택적으로 기대 GPU 이름을 지정하도록 네 소비자를 보강했으며, 기존 probe 호출과
실제 workload/timer 코드는 유지했다. SDK는 이름이 유일한 후보를 고른 뒤 device를 생성하고,
SDL은 선택 결과가 기대 이름과 다르면 workload 실행 전에 거부한다.

장치 귀속은 native Vulkan과 DXGI의 유일한 정확한 이름 일치 및 ID/LUID 교차 검사다.
SDL native device handle을 직접 확인한 것은 아니다. Intel device를 생성하는 검사는 실행하지 않았다.
이번 입력 고정은 Windows NVIDIA 조합에 한정되며 Linux 물리 GPU·수동 표시 수용을 포함하지 않는다.

**성능 sample 0개, 새 정확성/성능 승인 없음, 과거 timing 재귀속 없음, 전체 SDK 순위 없음.**
명령 receipt의 elapsed seconds는 빌드·프로세스 진단 시간이다. 성능 통계로 사용하지 않는다.
P3-BEN-02에서는 같은 입력의 full changing readback·필수 validation, 분리된 process,
교차 반복·warmup과 P50/P95/P99/MAD·원본 sample 보존을 먼저 연결해야 한다.

## 검증·원본 근거

- Python 전체 discovery **766개 통과**, skip 0. 새 계약 검사 16개에 실제 SDL `/GL-`와 SDK/SDL
  거부 메시지 회귀 검사를 포함했다. [전체 원본 로그](../benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/python-local/unittest.log)
- 같은 소스 커밋의 [Windows·Linux portable core CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40439008512)와
  [Linux Vulkan native compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40439008501)가 모두 성공했다.
  [원본 CI 상태](../benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/ci-runs.json)를 보존했다.
- [최종 raw receipt](../benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/accepted/receipt.json),
  [actual input SHA256 목록](../benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/accepted/inputs.json),
  [결과 요약](../benchmarks/reports/2026-10-27-phase3-comparison-inputs/summary.json)
- 첫 시도는 SDL의 명시적 IPO 비활성화 `/GL-`를 잘못 거부했다.
  [실패 기록](../benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/failed-flags/receipt.json)을 보존했다.
  두 번째는 upload SDK의 정확한 `adapter required` 오류를 `GPU` 단어 검사로 잘못 거부했다.
  [실패 기록과 실행 도구 revision](../benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/failed-target-message/source-context.json)을 보존했다.
  두 경우 모두 새 실행의 성공으로 바꾸지 않고 별도로 남겼다.
- [Windows 실행 소스 manifest](../benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/source-manifest-windows.json)와
  [Linux Git bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/source-manifest-linux-git.json)는
  각각 64파일 ZIP에 연결한다. SDK 28파일은 직전 source ZIP과 byte 일치한다. Linux snapshot은 Linux benchmark 실행 증거가 아니다.
  configure에 필요한 reference test cpp는 Git bytes로만 포함했으며 이 준비 작업에서는 해당 target을 빌드·실행하지 않았다.
  실행 파일·외부 라이브러리·driver binary는 재배포하지 않았다.

재실행 방법은 [준비 도구 안내](../benchmarks/phase3_comparison/README.md)를 따른다. 저장된 receipt 검사와
현재 local inputs까지 확인하는 `--live` 검사를 구분한다. 로드맵에서는 `P3-BEN-01` 하나만 완료로 바꾼다.

전체 보관 자료는 저장소 root에서 `python benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/verify.py`로
다시 검사할 수 있다. 이 [offline 검사](../benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/verify.py)는
원본 inventory·연결된 라이브러리·실제 명령별 Release 옵션·장치 식별·소스 ZIP·CI 상태를 확인하며 GPU workload를 실행하지 않는다.
