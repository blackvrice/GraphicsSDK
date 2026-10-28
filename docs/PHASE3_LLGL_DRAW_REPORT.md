# P3-CMP-04 LLGL Vulkan draw/instancing 정확성

2026-10-28. **승인된 `LLGL 649c0cc + transfer-src patch v1` 프로필로 완료**했다. draw 1회, draw 1,000회, 1,000 instances를 기존 SDK/SDL과 같은 SPIR-V·65×37 RGBA8·scissor (5,3,55,31)로 실행했다. 일반/required core+sync 검증 6개 프로세스에서 24개 이미지 전체 참조가 일치했다. native `vkCmdDraw` 호출·인수는 별도의 전달 관측 레이어에서 확인했다. 성능 표본은 0개다.

## 실제 관측

각 프로세스는 warmup 1회와 확인 3회 모두 전체 이미지를 읽는다. 아래 수치는 프로세스 하나의 4회 합계이며 plain/validation 두 모드에서 각각 확인했다.

| 입력 | native draw calls | native instance 인수 합 | native vertex 인수 합 | 전체 읽기 |
|---|---:|---:|---:|---:|
| draw1 | 4 | 4 | 12 | 4개 이미지·38,480 bytes |
| draw1000 | 4,000 | 4,000 | 12,000 | 4개 이미지·38,480 bytes |
| instanced1000 | 4 | 4,000 | 12,000 | 4개 이미지·38,480 bytes |
| 두 모드 합계 | **8,016** | **16,008** | **48,024** | **24개·230,880 bytes** |

각 호출의 vertex count 3, first vertex/instance 0, native instance min/max도 확인했다. 같은 triangle을 겹쳐 그리므로 이미지가 같아도 호출/instance를 하나 줄이면 관측 인수 검사에서 실패한다. 모든 정상 호출은 관측 레이어가 실제 다음 Vulkan 함수로 전달했으며 forwarded count가 일치한다. 이는 native API 인수 관측이며 GPU pipeline statistics로 측정한 primitive 수라는 주장은 아니다.

실제 GPU는 NVIDIA GeForce GTX 1660 SUPER, device UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, driverVersion 2604679168, queue family/index 0이다. 모든 정상/진단 프로세스의 native identity가 일치했다. 이번에는 `vkCreateInstance`의 application API 요청도 전달 관측했으며 **1.4.357 (4211045)**이었다. 기존 SDK/SDL draw의 명시적 1.1 요청과 구분한다. 같은 요청이라고 표시하지 않는다.

## 공개 API 대응과 차이

shader는 기존 `tests/fixtures/SpirvFixtures.hpp`의 fullscreen/red/green SPIR-V를 그대로 사용한다. 임의로 다시 컴파일하거나 다른 shader로 바꾸지 않았다. C++ 기존 `Frame.hpp`의 전체 참조와 독립 Python 참조 SHA-256이 일치한다. red/blue와 green/cyan을 매회 교대하고, 모든 2,405 pixels의 RGBA 4 bytes를 비교한다. shader header 및 원본 SPIR-V assembly도 source fingerprint에 연결한다.

LLGL은 offscreen `RenderTarget`의 자동 render pass와 공개 `Clear`를 사용한다. 전체 attachment를 먼저 clear하고 viewport·scissor·pipeline을 설정한 뒤 `Draw` 또는 `DrawInstanced`를 호출한다. 기존 SDK/SDL의 load-op clear와 공개 API 호출 형태가 다르므로, 향후 record/total CPU 경계에 이 clear 호출을 포함하고 차이를 공개해야 한다. target은 `ColorAttachment|Sampled|CopySrc`이며, 자동 pass의 최종 layout과 내부 tracking이 일치한다. SDK/SDL과 native usage/layout까지 동일하다고 주장하지 않는다.

명시적 `CreateRenderPass` 경로는 이 고정 Vulkan 구현에서 color final layout을 PRESENT_SRC로 만드는 경로가 있어 offscreen 대상으로 사용하지 않았다. 검증은 이를 검출했다. 정상 offscreen 공개 경로를 사용해 해결했고, **승인된 10줄 외에 LLGL 소스를 추가 수정하지 않았다**. 일반 실행에서 화면이 맞았다는 이유로 validation 오류를 무시하지 않았다.

draw 제출 뒤 공개 fence 완료를 기다리고, 측정 밖의 공개 `CopyBufferFromTexture` → fence 완료 → `ReadBuffer`로 전체 결과를 얻는다. 실제 workload는 LLGL 공개 API만 사용하며 native Vulkan으로 draw/전송/barrier를 대신하지 않는다. native 조회는 귀속, 관측은 전달 확인, 잘못된 native 호출은 별도 validation 통제에만 한정한다.

## 오류 통제와 관측 한계

matrix는 정상 6개, core/sync 진단 2개, 거부 8개로 총 16개 프로세스다. 거부 항목은 missing layer, wrong target, draw 1개 누락, instance 1개 누락, wrong scissor, readback 마지막 byte 변조, observer 비활성화, sync 비활성화다. 각 경우 의도한 오류 문구·exit 1과 성공 JSON 부재를 확인했다.

core 통제는 잘못된 properties sType의 VUID, sync 통제는 write-after-write diagnostic을 확인한다. sync 통제 명령은 record만 하고 제출하지 않으며, sync를 끄면 요구 diagnostic 부재로 실패한다. 정상 workload의 delivered ERROR는 0이다. teardown 후 관측된 live instance/device도 0이다.

자체 observer는 process-local explicit layer이며 actual `vkCmdDraw`를 그대로 전달한다. manifest는 loader가 요구하는 문자열 `implementation_version`을 사용하고, 실제 layer 활성화를 확인하지 못하면 실패한다. 현재 loader가 application API 1.4.357을 요청했음을 관측하지만, 이는 observer가 Vulkan 1.4 전체의 별도 conformance를 검증했다는 뜻은 아니다.

observer는 CPU 비용을 추가하므로 **이 실행은 성능 비교로 사용하지 않는다**. 향후 `P3-CMP-08`은 별도 observer-free 프로세스·fresh 세 SDK 실행·raw 표본·CPU 경계·API 차이를 확인해야 한다. CPU fence wait를 GPU timestamp로 바꾸어 표시하지 않으며 `P3-BEN-09`는 필수 미완료다.

## 빌드 입력과 재검사

새 consumer/observer compiler read 2개, linker read 2개와 실제 `/O2 /MD` no-LTO command 2개를 감사했다. 실제 파일 386개는 LLGL header 86, Vulkan header 13, 자체 draw/shared context/fixture 6, 시스템 입력 279, 기존 감사 import library 2개다. 고정 checkout·기존 DLL·승인 patch와 새 compiler/linker 입력을 연결하고, 총 794개 source/tool/artifact 기록을 실행 전후 대조했다. 시스템/driver·고정 VVL·현재 consumer/observer·승인 LLGL DLL 외 runtime 모듈은 거부한다. source·라이선스·metadata만 저장하며 외부 DLL/EXE/driver를 재배포하지 않는다.

```text
python tools/llgl_vulkan_draw.py --build-root out/NEW_DRAW/build --output out/NEW_RESULT --gpu-name "NVIDIA GeForce GTX 1660 SUPER"
python tools/check_llgl_draw_profile.py
python benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/verify.py
python -m unittest discover -s tests -p test_llgl_vulkan_draw.py -v
python tools/roadmap.py --check
```

새 빌드는 `benchmarks/llgl_vulkan_draw`의 Windows x64 Release CMake project를 사용하며 승인된 기존 DLL build와 고정 VVL prerequisite가 필요하다. [native receipt](../benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/native/receipt.json), [입력](../benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/native/inputs.json), [source ZIP](../benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/source.zip), [summary](../benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/summary.json), [독립 verifier](../benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/verify.py)를 보존한다. offline 재검사는 저장된 metadata·원본 해시·전체 참조·native 인수를 확인하며 GPU 실행이나 dependency 재획득을 대신하지 않는다. metadata 해시는 외부 신뢰 서명이 아니다.

새 회귀 테스트 28개와 전체 Python 테스트 984개가 통과했다. 잘못된 native 호출/instance 수, partial/변하지 않는 이미지, 누락된 관측, stale shader/source, 성능 수용 위조와 증거 변조를 거부한다. Windows/Linux CI에서도 프로필 확장과 저장된 native 관측을 독립 재검사한다.

기존 업로드 선정/수용 계약은 역사적 기록으로 보존하고, 이번 draw 결과는 별도 [프로필 확장](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-draw.json)으로 연결한다. `P3-CMP-04`만 완료하며 다음은 `P3-CMP-05` PSO correctness다. 나머지 correctness·성능·Linux 물리 GPU·최종 gate의 완료 기준은 줄이지 않았다. 메모리/Intel 충돌 조사는 재개하지 않았다.
