# P6-POOL-03 Vulkan 4슬롯 후보 회귀 검증

검증 대상은 [P6-POOL-02](PHASE6_VULKAN_POOL_FOUR_SLOT.md)의 설치 SDK
`544fa455e751d4908846d38fe2e5701b05d6e34986527fc16ed5cb8b4ee384af`다.
Windows x64 Release에서 기존 Vulkan 관측·오류 주입 계층을 사용한다. 대상 SDK
fingerprint, 실제 장치와 드라이버의 식별자, validation layer 파일을 각 실행에서
확인한다. 합성 OOM은 관측 계층이 반환하며 실제 메모리 고갈을 유도하지 않는다.

## 검증 범위

| 시험 | 확인한 계약 |
|---|---|
| 혼합 churn | 24개 크기, 16라운드, 최대 96개 버퍼. 생존 버퍼의 전체 데이터와 native binding, 메모리 통계, 실패한 bind의 정리를 대조한다. |
| 고정 희소 점유 | [P6-POOL-01](PHASE6_VULKAN_POOL_WORKLOAD.md)의 논리 순번을 사용해 20개를 남긴다. 빈 슬롯 재충전과 전체 해제 뒤 4KiB 단일 할당을 확인한다. |
| 기록·제출 보유 | 원본 24개의 wrapper를 해제하고 대체 버퍼 72개에 다른 데이터를 기록한다. 이전 기록을 제출하고 fence를 기다린 뒤 원본과 대체 버퍼의 데이터를 모두 확인한다. |
| 4슬롯 이후 확장 | 4KiB 버퍼 7개로 1·2·4슬롯 블록을 채운 뒤 16KiB 확장에 host/device OOM을 주입한다. 4KiB 독립 대체 할당, 확장과 대체 할당이 모두 실패했을 때의 정리, 이후 16KiB 확장 재시도를 검사한다. |
| 기존 장애 행렬 | 이전 15개 할당·bind 사례와 혼합 컨텍스트 7개를 현재 후보에 다시 연결한다. 실패한 출력 소유권, 기존 바인딩·데이터 보존, 정확한 오류 전달과 최종 반환을 확인한다. |

Churn 소비자와 모델은 원본에서 별도 빌드 디렉터리로 생성한다. 모델에 4슬롯 상한을
적용하고 희소 생존 규칙을 고정한다. 기존 두 장애 소비자는 같은 SDK 설치본에 연결한다.
결과 검증기는 기존 결과 형식과 검사 경로를 재사용하며, 이번 실행에 후보 fingerprint와
고정 희소 프로필을 명시한다. [준비·실행 도구](../tools/run_vulkan_pool_four_slot.py)

## 결과

NVIDIA GTX 1660 SUPER와 Intel UHD Graphics 770에서 validation ON/OFF를 각각
실행했다. 조합마다 26개 시험이 통과해 **정상 프로세스 104개**를 검증했다.
계층 누락과 잘못된 장치 선택을 주입한 6개 음성 대조군은 결과 없이 실패했다.
장치별로 validation을 켜고 껐을 때의 작업량과 native 할당 집계가 일치했다.

- 버퍼 전체 readback **17,368회 / 56,977,696바이트**와 합성 오류 **460회**를
  검증했다. Vulkan 버퍼 **39,388개**를 생성·파괴했고, 성공한 메모리 할당과 반환은
  각각 **32,512회 / 134,753,280바이트**였다. 마지막 native 소유량, 미소비 오류,
  무효 해제, 바인딩 가드 오류와 장치 파괴에 의한 암묵적 정리는 모두 0이었다.
- 20개 고정 생존자의 요구량은 27,200바이트, 실제 예약량은 **81,600바이트**였다.
  버퍼 32개를 추가하는 동안 대상 블록 예약량과 성장 횟수는 그대로였으며, 전체 해제
  뒤에는 0바이트로 돌아왔다. 두 장치와 validation ON/OFF에서 동일했다.
- 상한에 도달한 다음 확장 요청은 **16KiB**였다. 주입한 OOM 뒤 **4KiB** 대체 할당에
  성공했고, 이중 OOM에서는 원래 소유량을 보존했다. 정상 재시도는 다시 16KiB를
  예약했다. 검증기는 주입 요청을 32KiB로 바꾸거나 rollback 예약량을 늘린 자료를
  거부했다.
- CPU에서 슬롯 정책 146개, 독립 churn 모델 172개, 오류 계획 914개 assertion을
  통과했다.

[실행별 검증 요약과 원본 stdout 해시](../benchmarks/reports/validation/2026-11-09-vulkan-four-slot/summary.json)

## 재현

[격리 후보](PHASE6_VULKAN_POOL_FOUR_SLOT.md)를 생성하고 Release 설치본을 준비한다.
Windows의 고정 Vulkan validation runtime은 기존
[준비 도구](../tools/prepare_vulkan_validation.py)로 생성한다. 아래 명령은 저장소
루트에서 실행한다.

```powershell
$repo = (Resolve-Path .).Path
$base = "$repo/out/pool03"
$sdk = "$repo/out/pool-four-install"
$src = "$repo/out/pool-four-slot"
python tools/run_vulkan_pool_four_slot.py prepare --output "$base/churn-source"
cmake -S experiments/vulkan_pool_four_slot/regression -B "$base/churn" -G "Visual Studio 17 2022" -A x64 "-DGFX_INSTALLED_SDK=$sdk" "-DGFX_OVERLAY=$base/churn-source"
cmake -S experiments/vulkan_buffer_pool_fault -B "$base/regression" -G "Visual Studio 17 2022" -A x64 "-DGFX_INSTALLED_SDK=$sdk"
cmake -S experiments/vulkan_pool_rebase_fault -B "$base/mixed-fault" -G "Visual Studio 17 2022" -A x64 "-DGFX_INSTALLED_SDK=$sdk"
foreach ($name in 'churn', 'regression', 'mixed-fault') {
    cmake --build "$base/$name" --config Release --parallel 1 -- /nr:false
    if ($LASTEXITCODE) { throw "Build failed for $name" }
}
& "$base/churn/Release/vulkan_four_slot_model.exe"
& "$base/regression/Release/vulkan_fault_plan.exe"
python tools/run_vulkan_pool_four_slot.py run --build "$base" --sdk-source "$src" --output "$base/matrix"
```

실행기는 GPU와 validation ON/OFF 조합마다 새 프로세스를 시작한다.
로컬 `matrix/`에 명령 목록, 전체 native trace를 담은 stdout, 진단 및 검증한 요약을
보관한다. 이번 시험은 정확성과 소유권 확인을 위한 것이며, 성능 비교는
`P6-POOL-04`의 별도 실행에서 진행한다.
