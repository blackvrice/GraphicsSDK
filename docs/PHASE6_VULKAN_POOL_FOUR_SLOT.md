# P6-POOL-02 Vulkan 4슬롯 격리 후보

[측정 계약](PHASE6_VULKAN_POOL_WORKLOAD.md)의 C 정책을 별도 빌드 소스로 구현했다.
블록의 최대 용량은 `min(4, 64 KiB / stride)`다. 첫 할당은 1슬롯으로 시작하고
빈 슬롯이 없으면 생존 블록의 최대 용량을 기준으로 1→2→4→4 순서로 증가한다.
이 상한을 성장 계산과 블록 생성 양쪽에서 적용한다. [정책 diff](../experiments/vulkan_pool_four_slot/four-slot.patch)

세 비교군은 같은 기준 커밋 `e75b658e5e61491f78519e714f6913123194fb48`에서 생성한다.
A는 dedicated, B는 기존 적응형 정책, C는 4슬롯 상한 정책이다. B에는 기존 소유권
검증을 거친 churn 패치를 적용하고, C는 그 결과의 `BufferPoolRange.hpp` 한 파일에만
정책 diff를 적용한다. 기존 적격성, 빈 슬롯 우선 재사용, 마지막 참조 해제 후 블록 반환,
할당 OOM의 exact-size fallback을 계승한다. 제품 소스와 공개 헤더, 기존 실험 소스는
별도로 보존한다. [기존 패치](../experiments/vulkan_pool_churn/candidate.patch)

## 소스 검증 기록

생성기는 작업 트리 대신 지정한 Git 커밋의 소스를 읽는다. Git 줄바꿈 설정을 고정하고
파일 경로 순서를 명시적으로 정렬해 Windows와 Linux에서 같은 바이트와 검증 기록을
얻는다. 새 `out/` 경로에만 생성하며, 각 파일의 SHA-256·크기, 적용한 패치와 변경
경로를 `candidate-receipt.json`에 남긴다. `--check`는 같은 소스를 다시 만들어 검증
기록과 실제 파일을 대조한다. [생성기](../tools/prepare_vulkan_pool_four_slot.py) ·
[검증 기록 해시](../experiments/vulkan_pool_four_slot/source-receipts.json)

| 정책 | SDK 소스 SHA-256 앞 12자리 | 기록한 파일 수 |
|---|---|---:|
| A dedicated | `8d6659e4464b` | 507 |
| B 기존 적응형 | `4bcbfda1951a` | 509 |
| C 4슬롯 | `544fa455e751` | 509 |

## 재현

Python 3.12 이상과 Git으로 세 소스를 생성한다. 각 출력은 새 디렉터리여야 한다.

```text
python tools/prepare_vulkan_pool_four_slot.py --policy dedicated --output out/pool-dedicated
python tools/prepare_vulkan_pool_four_slot.py --policy adaptive --output out/pool-adaptive
python tools/prepare_vulkan_pool_four_slot.py --policy four-slot --output out/pool-four-slot
python tools/prepare_vulkan_pool_four_slot.py --check --policy four-slot --output out/pool-four-slot
```

고정 Vulkan-Headers `b379292b2ab6df5771ba9870d53cf8b2c9295daf`를 사용한다.
아래 예의 `<Vulkan-Headers>`에는 해당 체크아웃의 절대 경로를, `<repo>`에는 저장소
절대 경로를 넣는다. Linux에서는 두 configure 명령에
`-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14`를 추가한다.
Windows에서는 `-G "Visual Studio 17 2022" -A x64`를 사용했다.

```text
cmake -S out/pool-four-slot -B out/pool-four-build -DGFX_ENABLE_VULKAN=ON -DGFX_ENABLE_D3D12=OFF -DGFX_BUILD_TESTS=OFF -DGFX_BUILD_EXAMPLES=OFF "-DGFX_VULKAN_HEADERS=<Vulkan-Headers>"
cmake --build out/pool-four-build --config Release
cmake --install out/pool-four-build --config Release --prefix out/pool-four-install
cmake -S experiments/vulkan_pool_four_slot -B out/pool-four-range "-DPOOL_SOURCE=<repo>/out/pool-four-slot" "-DCMAKE_PREFIX_PATH=<repo>/out/pool-four-install"
cmake --build out/pool-four-range --config Release
ctest --test-dir out/pool-four-range -C Release --output-on-failure -V
```

## 검증 결과

- Windows x64 MSVC 19.44와 Linux x64 GCC 14.2 Release에서 후보를 각각 빌드·설치했다.
  설치 SDK의 fingerprint를 확인한 뒤 64~4096바이트의 일곱 stride에서 성장 상한,
  8슬롯 용량 거부, 슬롯 재사용, 중복 해제 거부, dedicated 조건을 플랫폼별 146개
  assertion으로 확인했다.
- Linux에서 Windows가 생성한 A/B/C 소스 세 벌의 receipt와 파일을 다시 검증했다.
  기존 출력 경로, `out/` 바깥 경로, 8슬롯으로 수정한 소스 사본을 검사기가 거부했다.
- Linux lavapipe에서 기존 `vulkan_transfer`의 24회 전송과 28,521,273바이트 readback,
  `vulkan_creation_ownership`의 CPU 검사 3,640개가 통과했다. 전송은 validation layer를
  끈 소프트웨어 ICD에서 실행했다. 재현 시 SDK 구성에서 `GFX_BUILD_TESTS=ON`을 사용해
  `gfx_vulkan_tests`와 `gfx_vulkan_creation_tests`를 빌드하고, lavapipe ICD를 지정한
  환경에서 해당 두 CTest를 실행한다.

같은 workload의 native 예약량 실측, 혼합·보유와 합성 OOM/fallback 회귀는 후속
검증에서 진행한다.
