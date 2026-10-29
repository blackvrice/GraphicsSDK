# P3-CMP-05 LLGL Vulkan PSO 정확성

2026-10-28 실제 GPU 검증, 2026-10-29 완료 정리. **사용자가 승인한 `LLGL 649c0cc + transfer-src patch v1` 프로필로 완료**했다. 기존 GraphicsSDK/SDL과 같은 PSO 1개·8개, SPIR-V, 65×37 RGBA8와 scissor (5,3,55,31)를 사용했다. 일반/required core+sync 모드의 정상 실행 4개에서 전체 이미지 16개가 일치했고 validation ERROR는 0이다. 성능 표본은 0개다.

## 생성·사용·공개 해제

매 반복마다 새로운 PSO 공개 handle을 만들고, 같은 반복의 live handle이 서로 다른지 확인한다. 각 PSO마다 fullscreen VS와 교대 red/green FS를 생성하고 PSO 생성 직후 shader handle을 공개 해제한다. pipeline 생성 경계에는 shader 객체 생성·해제가 포함된다. 이 작업은 현재 정확성 실행이며 CPU 구간을 측정하지 않는다. 향후 측정은 기존 PSO 계약에 따라 descriptor/payload 준비를 생성 구간 밖에 둬야 한다.

PSO 8개는 각각 다른 stripe를 그린다. x 경계는 5,11,18,25,32,39,46,53,60이고 y 범위는 [3,34)다. shader 색은 반복·stripe 번호의 합에 따라 red/green으로 바뀌며 바깥 clear는 blue/cyan을 교대한다. 마지막 stripe까지 모든 2,405 pixels의 RGBA 4 bytes를 읽어 기존 C++ 전체 참조와 독립 Python 참조를 확인한다. 이미지가 같아도 draw 누락이 숨겨지지 않도록 실제 전달된 native draw 호출·인수도 관측한다.

GPU 사용 후 공개 fence 완료와 idle을 기다린다. 사용 command/fence를 먼저 해제하고 PSO 공개 handle을 모두 해제한 뒤, 별도 공개 copy/readback 경로로 전체 결과를 확인한다. 원본 command가 PSO를 계속 참조하는 상태에서 해제를 완료했다고 표시하지 않는다. warmup 1회와 확인 3회 모두 이 순서를 따른다.

아래는 프로세스 하나의 4회 합계다. 두 모드에서 각각 확인했다.

| 입력 | PSO 생성·사용·공개 해제 각각 | shader 생성·공개 해제 각각 | native draw/instances | native vertex 인수 | 전체 읽기 |
|---|---:|---:|---:|---:|---:|
| pso1 | 4 | 8 | 4 | 12 | 4개·38,480 bytes |
| pso8 | 32 | 64 | 32 | 96 | 4개·38,480 bytes |
| 두 모드 합계 | **72** | **144** | **72** | **216** | **16개·153,920 bytes** |

공개 handle 생성/해제 횟수는 adapter가 성공한 공개 API 호출로 센다. observer는 실제 `vkCmdDraw`가 다음 Vulkan 함수에 전달되는지와 인수를 확인한다. native `VkPipeline` 생성 개수, 내부 sharing/cache 상태, 물리 GPU 메모리 회수 여부는 검증하지 않았다. driver cache는 통제하지 않았으며 cold-cache 생성 비용이라는 주장을 하지 않는다.

## 환경·공개 API 차이

실제 GPU는 NVIDIA GeForce GTX 1660 SUPER, device UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, driverVersion 2604679168, queue family/index 0이다. 정상/진단 프로세스의 장치·driver·queue 귀속이 일치했다. 실제 관측한 instance API 요청은 1.4.357 (4211045)이며 기존 SDK/SDL의 명시적 1.1 요청과 구분한다.

shader는 `tests/fixtures/SpirvFixtures.hpp`를 그대로 사용한다. 원본 assembly와 header를 source fingerprint에 연결했다. native workload를 Vulkan으로 대신 생성·그리지 않고 LLGL 공개 API로 수행한다. native 접근은 장치 귀속 조회, 전달 관측, 별도 validation 진단 통제에 한정한다.

LLGL은 자동 offscreen render pass와 전체 attachment 공개 `Clear`를 사용하며 target bind flags는 `ColorAttachment|Sampled|CopySrc`다. 기존 SDK/SDL의 load-op clear와 호출·usage/layout이 다르다. 이전 draw에서 확인한 정상 공개 경로를 재사용했고 승인된 10줄 외에 LLGL 코드를 추가 수정하지 않았다. 이미지·작업 의미와 공개 생성/해제 경계의 대응은 확인했지만 native driver cache/할당까지 동일하다는 근거는 아니다.

이번 결과는 PSO 정확성의 수용 근거다. 세 SDK의 성능 비교 수용은 별도 `P3-CMP-09`에서 fresh 실행·CPU 경계·API 차이·환경 귀속을 확인한 뒤 결정한다. pass-through observer의 비용 때문에 이번 실행을 성능 표본으로 재사용하지 않는다. CPU fence 시간을 GPU timestamp로 표시하지 않으며 `P3-BEN-09`는 필수 미완료다.

## 실패 검증과 증거

총 16개 프로세스는 정상 4개, core/sync 진단 2개, 거부 10개다. 거부 항목은 missing layer, wrong target, pipeline 생성 누락, 공개 해제 누락, 잘못된 PSO 적용, stripe draw 누락, wrong scissor, readback 마지막 byte 변조, observer 비활성화, sync 비활성화다. 각 거부는 지정된 오류·exit 1·성공 JSON 부재를 확인했다. 정상 결과처럼 저장하지 않는다.

core 통제는 잘못된 properties sType VUID를, sync 통제는 `SYNC-HAZARD-WRITE-AFTER-WRITE`를 확인한다. 잘못된 sync 명령은 record만 하고 제출하지 않는다. sync를 끄면 요구 진단 부재로 실패한다. 정상 teardown 후 관측된 live native instance/device는 0이다.

PSO consumer compiler/linker read 각 1개와 실제 `/O2 /MD` no-LTO command 1개를 감사했다. 실제 입력 386개는 LLGL header 86, Vulkan header 12, 자체/shared/fixture 6, 시스템 280, 기존 감사 import library 2개다. 총 811개 source/tool/artifact를 실행 전후 대조했다. observer DLL/manifest는 CMP-04에서 감사한 것을 byte 단위로 그대로 재사용하고, 기존 draw/업로드 입력과 승인 DLL도 변경하지 않았다. runtime은 시스템/driver, 고정 VVL, 현재 consumer/observer, 승인 LLGL DLL만 허용한다.

외부 DLL/EXE/driver를 재배포하지 않고 source ZIP·라이선스·build/runtime 해시와 raw 출력을 보존한다. offline verifier는 ZIP 내부 코드를 실행하지 않고 저장한 전체 stripe 참조·native 인수·공개 lifecycle 횟수·원본 해시를 독립 재검사한다. offline 검사는 GPU 재실행이나 dependency 재획득을 대신하지 않으며 metadata 해시는 외부 신뢰 서명이 아니다.

```text
python tools/llgl_vulkan_pso.py --build-root out/NEW_PSO/build --output out/NEW_RESULT --gpu-name "NVIDIA GeForce GTX 1660 SUPER"
python tools/check_llgl_pso_profile.py
python benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/verify.py
python -m unittest discover -s tests -p test_llgl_vulkan_pso.py -v
python tools/roadmap.py --check
```

새 빌드는 `benchmarks/llgl_vulkan_pso`의 Windows x64 Release CMake project를 사용한다. 승인 DLL, CMP-04 observer build, 고정 VVL prerequisite가 필요하다. [native receipt](../benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/native/receipt.json), [입력](../benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/native/inputs.json), [source ZIP](../benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/source.zip), [summary](../benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/summary.json), [독립 verifier](../benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/verify.py)를 보존한다.

새 회귀 테스트 30개는 lifecycle/참조/validation/장치 귀속 누락, 물리 회수·성능 수용 위조, 증거 변조를 거부한다. 전체 Python 테스트 **1,014개, 실패·오류·건너뜀 0개**가 통과했으며 입력은 실행 전후 일치했다. [전체 실행 receipt](../benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/python-receipt.json)는 기반 커밋과 staged 입력 상태를 기록하며 완성 커밋의 CI 결과와 구분한다. Windows/Linux CI에서도 프로필 확장과 저장된 PSO 증거를 독립 재검사한다. 기존 업로드·draw 계약은 역사적 기록으로 보존하고 별도 [PSO 확장](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-pso.json)으로 연결한다.

`P3-CMP-05`만 완료하며 다음은 `P3-CMP-06` buffer/texture 생성 정확성이다. 나머지 성능·Linux 물리 GPU·최종 gate의 완료 조건은 유지한다. 메모리/Intel 충돌 조사는 재개하지 않았다.
