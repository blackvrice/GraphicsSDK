# P3-CMP-06 LLGL Vulkan 자원 생성 정확성

2026-10-29. **사용자가 승인한 `LLGL 649c0cc + transfer-src patch v1` 프로필로 완료**했다. 기존 GraphicsSDK/SDL의 버퍼·텍스처 1개/1,000개 계약을 LLGL 공개 API로 실행했다. 정상 8개 프로세스에서 버퍼 8,008개·텍스처 8,008개, 총 109,837,728바이트의 전체 참조가 일치했다. required core+sync 실행의 validation ERROR는 0이다. 성능 표본은 0개다.

## 자원 생성·사용·공개 해제

버퍼는 4,096바이트, 텍스처는 65×37 RGBA8 2D·mip/layer/sample 각 1개다. 각 반복마다 대상 자원을 새로 생성한다. 동시에 살아 있는 공개 handle이 서로 다르고 실제 native buffer/image handle도 서로 다른지 조회한다. 텍스처의 native format·extent·mip/layer/sample·sampled/copy usage도 확인한다. 서로 다른 자원이 같은 memory allocation을 공유하는 것은 허용하며 memory allocation 횟수와 구분한다.

기존 `benchmarks/vulkan_allocation/Payload.hpp`의 uint32 wrapping 알고리즘을 그대로 사용한다. 반복 번호·자원 index·byte offset 모두 payload에 반영된다. 모든 자원을 공개 GPU copy로 업로드하고 readback한 뒤 **각 자원의 모든 바이트**를 비교한다. 실제 readback마다 SHA-256을 저장하고 독립 Python 참조로 전부 재검사한다. 마지막 자원의 마지막 바이트도 검증하며, 텍스처는 row padding을 제외한 tight 9,620바이트다.

각 프로세스는 warmup 1회와 확인 3회 모두 생성·전체 GPU 사용·검증·공개 해제를 수행한다. 아래 수치는 프로세스 하나의 4회 합계로 plain/validation에서 각각 확인했다.

| 입력 | 생성·사용·검증·공개 해제 각각 | 전체 읽기 |
|---|---:|---:|
| buffer1 | 4 | 16,384 bytes |
| buffer1000 | 4,000 | 16,384,000 bytes |
| texture1 | 4 | 38,480 bytes |
| texture1000 | 4,000 | 38,480,000 bytes |
| 두 모드 합계 | **16,016** | **109,837,728 bytes** |

공개 해제 전에 제출 명령의 fence/idle 완료를 기다리고 command/fence를 해제한다. 모든 readback 비교 후 staging 자원을 해제한 다음 대상 자원을 공개 해제한다. 각 단계의 성공 횟수가 일치해야 한다. 공개 해제 성공을 물리 GPU 메모리 회수로 표시하지 않는다. native memory allocation 수와 allocator/cache 상태도 미검증이다.

## SDK·SDL 대응과 차이

LLGL 버퍼는 CPU access 0, `VertexBuffer|CopySrc|CopyDst`다. 기존 SDK는 general device buffer, SDL은 vertex buffer 용도다. 텍스처는 세 경로 모두 sampled RGBA8 2D 용도를 따른다. 공개 기본 생성 정책을 사용하고 초기 payload 포인터를 전달하지 않는다. LLGL의 기본 생성 구현은 내부 staging/초기화를 포함할 수 있으며, 향후 공개 생성 시간에는 해당 구현 비용도 포함된다. 내부 heap·native usage bit·allocator 상태까지 동일하다는 주장은 아니다.

고정 LLGL backend의 공개 `CopyBuffer`는 transfer-write→read barrier를 자동으로 넣지 않는다. 공개 `ResourceBarrier`는 shader read/write 용도이므로 전송 barrier로 대신 사용하지 않았다. **업로드와 다운로드를 각각 공개 fence·idle로 완료하는 별도 제출로 나눴다.** 이 경로의 실제 required sync 검증과 전체 결과가 통과했다. SDK/SDL의 같은 command 내 전송과 제출 구조가 다르며 GPU 처리량 비교 근거로 사용하지 않는다. LLGL workload를 native Vulkan copy/barrier로 대체하거나 승인된 10줄 외에 LLGL 코드를 추가 수정하지 않았다.

기존 자원 비교 계약의 `create_ms`·`public_release_ms` 의미에 대응할 수 있다. descriptor/vector 준비, staging 생성·해제, payload 준비·쓰기, native identity 조회·중복 검사, GPU copy, fence/idle, 전체 readback, 최종 teardown은 향후 두 측정 구간 밖이어야 한다. 현재 adapter는 정확성 확인만 하므로 시간 표본을 만들지 않는다. **세 SDK 성능 수용은 `P3-CMP-10`에서 fresh 실행과 측정 경계·환경·API 차이를 다시 확인해야 한다.** 전체 SDK 순위나 GPU timestamp 결과로 확대하지 않는다.

실제 GPU는 NVIDIA GeForce GTX 1660 SUPER, device UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, driverVersion 2604679168, queue family/index 0이다. 정상/진단 프로세스의 native identity가 일치했다. 기존 CMP-04 observer DLL/manifest를 byte 그대로 재사용해 instance API 요청 1.4.357 (4211045)과 native instance/device teardown을 관측했다. SDK/SDL의 명시적 1.1 요청과 구분한다. 이 observer가 allocation/copy 호출을 센다는 주장은 하지 않는다. 자원 handle은 별도 공개 native 조회로 확인한다.

## 오류 통제와 보존 근거

총 22개 프로세스는 정상 8개, core/sync 진단 2개, 거부 12개다. 거부 항목은 missing layer, wrong target, 자원 생성 누락, live 자원 handle 중복, 공개 해제 누락, 다른 자원 payload, 이전 반복 payload, 마지막 readback copy 누락, 마지막 byte 변조, 읽기 길이 누락, observer 비활성화, sync 비활성화다. 각 거부는 지정된 오류·exit 1·성공 JSON 부재를 확인했다. workload 실패 통제는 2개 자원만 사용하는 별도 소규모 프로세스다.

core 통제는 잘못된 properties sType VUID를, sync 통제는 `SYNC-HAZARD-WRITE-AFTER-WRITE`를 확인한다. 잘못된 sync 명령은 record만 하고 제출하지 않는다. sync를 끄면 요구 진단 부재로 실패한다. 정상 teardown 후 관측된 live native instance/device는 0이다.

새 consumer compiler/linker read 각 1개와 실제 `/O2 /MD` no-LTO command 1개를 감사했다. 실제 입력 385개는 LLGL header 86, Vulkan header 12, 자체/shared/payload 5, 시스템 280, 기존 감사 import library 2개다. 총 827개 source/tool/artifact를 실행 전후 대조했다. 기존 업로드·draw·PSO 소스/빌드와 승인 patch/DLL·고정 checkout은 변경하지 않았다. runtime은 시스템/driver·고정 VVL·현재 consumer/observer·승인 LLGL DLL만 허용한다.

외부 DLL/EXE/driver를 재배포하지 않고 raw stdout/stderr, 입력 해시·build 로그·라이선스·source ZIP을 보존한다. 독립 verifier는 ZIP 코드를 실행하지 않고 모든 자원/반복의 전체 payload hash와 lifecycle·통제·입력 해시를 재검사한다. native fingerprint는 실제 Windows 빌드 bytes를 유지한다. 현재 소스의 운영체제별 Git 줄바꿈만 내용 비교에서 허용하고, 별도 임시 복원한 ZIP bytes로 원래 fingerprint를 계산한다. checkout과 GPU 기록은 재작성하지 않는다. offline 검사는 실제 GPU 재실행을 대신하지 않으며 metadata 해시는 외부 신뢰 서명이 아니다.

```text
python tools/llgl_vulkan_allocation.py --build-root out/NEW_ALLOCATION/build --output out/NEW_RESULT --gpu-name "NVIDIA GeForce GTX 1660 SUPER"
python tools/check_llgl_allocation_profile.py
python benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/verify.py
python -m unittest discover -s tests -p test_llgl_vulkan_allocation.py -v
python tools/roadmap.py --check
```

새 빌드는 `benchmarks/llgl_vulkan_allocation`의 Windows x64 Release CMake project를 사용한다. 승인 DLL·CMP-04 observer build·고정 VVL prerequisite가 필요하다. [native receipt](../benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/native/receipt.json), [입력](../benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/native/inputs.json), [source ZIP](../benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/source.zip), [summary](../benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/summary.json), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/verify.py)를 보존한다.

새 회귀 테스트 31개는 전체 payload·lifecycle·native identity·validation 누락, 물리 회수·성능 수용 위조, 증거 변조를 거부한다. 전체 Python 테스트 **1,045개, 실패·오류·건너뜀 0개**가 통과했고 실행 전후 입력도 일치했다. [전체 실행 receipt](../benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/python-receipt.json)는 기반 커밋과 staged 입력을 기록하며 최종 커밋의 CI 결과와 구분한다. Windows/Linux CI에서 새 프로필 확장과 저장된 증거도 독립 재검사한다. 기존 계약은 역사적 기록으로 보존하고 별도 [allocation 확장](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-allocation.json)으로 연결한다.

`P3-CMP-06`만 완료하며 다음은 `P3-CMP-07` 세 SDK 업로드 비교 실행이다. `P3-CMP-07`~`10`, `P3-BEN-09`, Linux 물리 GPU·최종 gate는 별도 미완료로 유지한다. 장시간 메모리/Intel 충돌 조사는 재개하지 않았다.
