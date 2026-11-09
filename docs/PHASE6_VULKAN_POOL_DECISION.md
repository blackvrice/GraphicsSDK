# P6-POOL-05 Vulkan 소형 버퍼 풀 채택 판정

**4슬롯 후보의 제품 승격을 기각했다.** 제품 Vulkan은 버퍼와 텍스처에 자원별
전용 할당을 사용한다. 4슬롯 코드는 같은 조건에서 다시 비교할 수 있는 격리
실험으로 분류한다. [격리 후보](PHASE6_VULKAN_POOL_FOUR_SLOT.md)

## 판단 근거

4슬롯 후보는 두 Windows GPU의 혼합·희소·기록 보유 프로필에서 전용 할당보다
생성·해제 p50이 빨랐다. 차이는 A/A 반복 편차를 넘었다. 합성 오류 460회,
기록·제출의 자원 보유, 해제 후 0바이트도 확인했다. [정확성 회귀](PHASE6_VULKAN_POOL_REGRESSION.md) ·
[330개 프로세스의 비교](PHASE6_VULKAN_POOL_PERFORMANCE.md)

| 동일한 작업의 메모리 비용 | 전용 할당 | 기존 적응형 | 4슬롯 |
|---|---:|---:|---:|
| 희소 점유 20개 생존, 예약 bytes | 27,200 | 168,640 | 81,600 |
| 혼합 churn, 최대 예약 bytes | 394,752 | 473,472 | 433,728 |
| 기록·제출 보유, 최대 예약 bytes | 394,752 | 433,728 | 433,728 |

4슬롯 후보는 기존 적응형의 희소 예약 비용을 약 52% 줄이면서 전용 할당 대비
할당 호출을 줄였다. 희소 시점에는 여전히 전용 할당의 3배를 예약했고, 혼합과
기록 보유의 최대 예약량은 전용 할당보다 약 9.9% 높았다. 기존 적응형의 생성·해제
p50 관측 중앙값은 4슬롯보다 낮았으며, Intel의 일부 p95는 반복 편차가 커 개선을
판정하기 어려웠다. 이 결과는 두 GPU의 Vulkan 할당 요청 기준이며 플랫폼 전체의
메모리 예산이나 물리 상주량은 별도 문제다.

기본값은 모든 Vulkan 소비자에게 영향을 준다. 관측한 지연 이점만으로 희소 점유의
메모리 비용을 일반 소비자에게 적용할 근거는 부족하다. 공개 opt-in도 장치 선택과
지원 조합을 늘리고, 공유 블록을 한 번만 세는 진단 계약과 장치별 회귀 범위가
필요하다. 이번 제품 범위는 전용 할당과 그 진단 계약으로 확정했다.

## 제품 API와 통계

`DeviceDesc`와 `ResourceStatistics`는 검증한 제품 헤더의 기존 형태를 사용한다.
Vulkan의 `uniqueResources`는 SDK가 보유한 고유 자원을 세며,
`resourceAllocationBytes`는 각 자원의 native 할당 요청 크기를 합한다.
기록과 제출이 보유한 자원은 해당 native 자원이 회수될 때까지 포함한다.
최종 제품 설치본의 공개 헤더와 컴파일 fingerprint로 이 범위를 확인했다.

## 최종 제품 검증

제품 커밋 `69030be228d7ea641a3ddae88d0772733623f756`의 추적 소스를 별도로
추출해 Windows MSVC 19.44와 Linux GCC 14.2에서 Vulkan Release를 새로
빌드·설치했다. 두 소비자가 보고한 SDK fingerprint는 전용 할당 기준군과 동일한
`8d6659e4464b47b810ca89a0b492946421c3b19d748962a20f4733cd4dad8c16`이었다.
두 설치본의 공개 헤더 13개를 제품 소스와 바이트 단위로 대조했다.

- Windows GTX 1660 SUPER와 Intel UHD 770에서 설치 소비자의 버퍼 전송 48회
  및 텍스처 왕복 74회를 validation ON/OFF 각각 실행했다. 버퍼 57,042,546바이트와
  텍스처 499,371,832바이트가 각 모드에서 일치했다.
- Linux lavapipe에서는 설치 소비자의 버퍼 전송 24회와 텍스처 왕복 37회가
  통과했다. native 생성 출력 소유권 CPU 검사 3,640개가 양쪽 플랫폼에서 통과했다.
- Windows의 세 프로필을 두 GPU에서 최종 제품 설치본으로 다시 관측했다.
  6개 실행의 대상 할당·반환 수, 보조 할당, 예약량 체크포인트가
  `P6-POOL-04`의 전용 할당 기준군과 일치했다. 각 체크포인트에서 대상 버퍼 수와
  native 할당 수가 일치했고, 예약량은 native 요구량의 합이었다. 전체 해제 뒤
  0바이트를 확인했다.

[제품 빌드와 관측 증거](../benchmarks/reports/validation/2026-11-09-vulkan-pool-decision/evidence.json)

### 재현

위 제품 커밋의 `CMakeLists.txt`, `LICENSE`, `cmake`, `examples`, `include`,
`shaders`, `src`, `tests`, `tools`를 새 디렉터리에 추출한다. Git archive의
`core.autocrlf=false`와 `core.eol=lf`로 소스 바이트를 고정했다.
[격리 후보 빌드 절차](PHASE6_VULKAN_POOL_FOUR_SLOT.md)와 같은 Vulkan-Headers와
Release 구성을 사용하고, 이 제품 빌드에는 `GFX_BUILD_TESTS=ON`을 지정한다.
`gfx_vulkan_creation_tests`를 빌드·실행한 뒤 설치본으로
`tests/vulkan_consumer`의 `vulkan_consumer`와 `vulkan_texture_consumer`를
빌드·실행한다. Windows는 고정 validation layer 경로를 지정해 두 소비자를
`--validation`으로도 실행한다. Linux는 lavapipe ICD를 지정한다.

Native 관측은 [성능 측정 소비자](PHASE6_VULKAN_POOL_PERFORMANCE.md)를 제품
설치본에 연결한 뒤 각 GPU의 `mixed`, `sparse`, `held` 프로필을
`pool_performance <프로필> "<정확한 GPU 이름>" observe`로 실행한다.
실행 프로세스의 `VK_LAYER_PATH`에는 절대 DLL 경로를 가진 관측 manifest의
디렉터리를 지정한다. `VK_INSTANCE_LAYERS=VK_LAYER_GFX_buffer_fault`와
`GFX_EXPECT_FAULT_LAYER`로 같은 관측 DLL을 선택한다.
