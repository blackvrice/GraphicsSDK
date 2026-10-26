# P3-CAP-02 — Vulkan generation recovery

2026-10-26. 사용자 지정 작업으로 조건부 기능을 선택해 구현·통제 검증을 완료했다.
구현 커밋은 `5d8af5c5632edbffa93bd8ab3c93ef4474c108e9`다.
초기 subset의 `caps=false/Unsupported` 결정을 확대했으며 public API/ABI 형식과 D3D12 구현은 변경하지 않았다.

## 동작과 실패 계약

Vulkan `caps.deviceRecovery=true`다. 생성 thread에서 관찰된 `VK_ERROR_DEVICE_LOST`만
복구하며 기존 VkDevice의 자원·pending recording을 퇴역시킨 뒤 새로운 Device를 만든다.
새 resource/fence generation은 이전과 다르다. 최초 자동 선택도 실제 선택된 UUID 기반
adapter ID로 고정하므로 열거 순서가 바뀌어도 GPU가 바뀌지 않는다. software/validation 설정도 유지한다.

이전 buffer/texture/pipeline/command/swapchain은 CPU 설명만 보유하며 native 조작은
기존 `DeviceLost/-4`로 차단된다. 새 generation은 이전 자원과 fence를 WrongDevice로 거부한다.
호출자는 보관한 CPU shader·asset 데이터로 자원을 다시 만들어야 한다. 내용 자동 복원은 없다.
퇴역한 VkDevice의 native child는 해제하되 instance/surface 같은 정리 부모는 이전 wrapper 해제까지
남을 수 있다. 이전 wrapper를 늦게 해제해도 새 generation은 정상 동작한다.

정상·다른 thread·성공한 이전 generation의 반복 recover는 InvalidState다. 재생성이
OutOfMemory 또는 DeviceLost로 실패하면 이전 generation은 손실·퇴역 상태로 남고 재시도가 가능하다.
재생성 실패가 이전 손실 원인을 덮어쓰지 않는다. adapter 부재 등 실패는 그대로 반환하며 다른 GPU로
fallback하지 않는다. `vkCreateFence`에서 관찰된 손실도 상태에 저장하도록 누락을 수정했다.

Khronos의 [device loss 계약](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html)에 따라
손실된 logical device를 정상으로 되돌리는 대신 새 logical device를 생성한다. 물리 장치가
계속 사용 가능하지 않으면 재생성 성공은 보장할 수 없다. 자식 객체도 명시적으로 정리한다.

## 통제 시험

[설치 SDK 소비자·자체 explicit layer](../tests/vulkan_recovery/README.md)는 실제 native 작업을
drain한 뒤 QueueSubmit/GetFenceStatus/CreateFence에서 한 번씩 합성 손실을 반환한다.
재생성 두 번에는 각각 OOM/DeviceLost를 반환하고 세 번째는 실제 VkDevice를 생성한다.
driver reset, 실제 TDR, 물리 손실이나 전역 layer 등록을 수행하지 않는다.

각 adapter에서 세 손실 경로를 실행하고 자동 선택을 한 번 추가한다. 다중 GPU 자동 선택에서는
실제 후보 열거 배열을 뒤집어 같은 UUID 복구를 확인한다. 기존 자원·펜스·wrapper를 보유한 채
퇴역 순서·차단·재시도·새 generation을 검사한다. 관측 hook의 native 작업 횟수 증가가 없음을
검사하며, 모든 Vulkan 함수가 계측됐다고 해석하지 않는다.

복구 전후 CPU SPIR-V와 패턴 데이터로 pipeline·buffer·texture를 재구축하고 실제 draw/copy를
제출한다. 33×17 RGBA8 전체 texel과 각 row padding sentinel, 4097-byte buffer 전체를 비교한다.
지원되는 Win32 장치는 이전 swapchain/backbuffer wrapper를 보유한 채 같은 숨김 HWND에
새 swapchain을 만들고 이미지 대조 후 실제 present한다. 사람의 화면 확인은 이 시험 범위가 아니다.

| 실행 | adapter | 복구 cycle | 실패 재생성 | 전체 이미지 / swapchain 이미지 | 검증 buffer bytes |
|---|---:|---:|---:|---:|---:|
| Windows 일반 | NVIDIA GTX 1660 SUPER + Intel UHD 770 | 7 | 14 | 18 / 4 | 57358 |
| Windows 필수 core/sync validation | 동일 2개 | 7 | 14 | 18 / 4 | 57358 |
| Linux llvmpipe 소프트웨어 ICD | 1개 | 4 | 8 | 8 / 0 | 32776 |

각 Windows 실행에서 native device 생성/파괴는 각각 14, Linux는 각각 8로 일치했다.
종료 시 live instance/device, layer 오류, 이전 device 파괴 전 조기 생성은 모두 0이다.
Windows 검증 실행은 고정 VVL 1.3.290의 core/sync/submit-time 설정과 빈 settings 경로를 사용했고
이전·새 device의 delivered validation ERROR 수가 0임을 확인했다. Linux 복구 실행은 driver-only이며
이 결과를 Linux required-validation 복구 검증으로 바꾸어 해석하지 않는다.

전체 native 회귀 **60/60**, 전체 Python **673개**가 로컬에서 통과했다. native 제외는 기존의
`memory_reclamation_gpu/validation` 두 항목뿐이다. 장시간 메모리 수용은 별도다.
같은 구현 커밋의 [Windows/portable CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40267223955)도
각각 Python 673개, 오류·실패·건너뜀 0으로 통과했다.
[Linux 복구 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40267223990)와
[Linux native compile](https://github.com/blackvrice/GraphicsSDK/actions/runs/40267224083)도 성공했다.
기타 Linux suite 상태는 원본 CI snapshot에 개별 기록하며 진행 중인 실행을 성공으로 합산하지 않는다.

첫 두 빌드는 새 시험 코드의 필수 clear 인수 누락과 존재하지 않는 footprint 필드 참조로
실패했다. 원본 source/log를 보존했고 수정 후 직렬 build/install과 위 회귀를 통과했다.
컴파일러 crash나 driver 문제로 분류하지 않는다.

## 정확한 소스와 재현

Windows 실제 실행 byte SDK fingerprint:
`763ad9630c74b011da5dba07c90495b908c543ae432d2b0b25a5c90e0022c5a8`.
Linux 해당 커밋 Git byte SDK fingerprint:
`ca2f63fadcfdf373bde663c747e0bed626d071f737131d6e2d6daee2ff7c0f9e`.
각각 SDK 27개와 시험/layer/fixture/도구를 포함한 41개 source 파일 ZIP, manifest와 해시를 보존했다.
두 fingerprint와 과거 marker 실행 source는 서로 바꾸어 쓰지 않는다.
외부 loader/ICD/VVL/컴파일 산출물은 배포하지 않고 식별 해시만 기록한다.

[원본·source manifest·오프라인 검증기](../benchmarks/reports/validation/2026-10-26-phase3-vulkan-recovery/verify_evidence.py)는
네트워크나 GPU 없이 archive 해시·전체 결과 row·실패 재시도/퇴역 계수·두 플랫폼 source·회귀 receipt를 재검사한다.
저장소 루트에서 다음 명령을 실행한다.

```powershell
python benchmarks/reports/validation/2026-10-26-phase3-vulkan-recovery/verify_evidence.py
```

통제 layer의 합성 손실은 public `deviceLossInjection` 구현이 아니다. Vulkan 해당 cap은 false이고
testing opt-in 생성·정상 device의 `removeForTesting()`는 Unsupported를 유지한다.
`P3-CAP-03`, 물리 Linux GPU `P3-LNX-15`, Linux WSI, Phase 2의 메모리/Intel AV/DPI/물리 손실,
Phase 3 최종 수용 항목은 이번 작업으로 완료되지 않는다.
