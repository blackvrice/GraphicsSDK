# Phase 3 — Linux 버퍼·queue/fence·수명 계약

2026-10-25 · `P3-LNX-05` · 검증 커밋 `5f544c0`.

실제 Ubuntu에서 기존 공통 `tests/VulkanTests.cpp`를 그대로 빌드해 in-tree CTest와 설치 SDK 소비자로 실행했다. **Linux software ICD의 버퍼 전송·대기·거부·수명 계약을 완료**한다. 관측 adapter는 CPU 종류의 Mesa lavapipe이며 물리 GPU는 0개다. 물리 GPU의 같은 계약 수용은 필수 `P3-LNX-15`에 유지한다.

[실행 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40173473104)와 [job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40173473104/job/121109086212)은 성공했다. SDK C++·공개 API·root `CMakeLists.txt`와 기존 C++ suite는 변경하지 않았다. [요약](../benchmarks/reports/2026-10-25-phase3-linux-transfer/summary.json), [원본 receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-transfer/native/receipt.json), [job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-transfer/job.log)에 결과를 보존했다.

## 실행 결과

| 실행 | Assertion | 전송 수 | 전체 대조한 payload bytes |
|---|---:|---:|---:|
| in-tree `vulkan_transfer` CTest | 361 | 24 | 28,521,273 |
| 설치 SDK `vulkan_consumer` | 361 | 24 | 28,521,273 |
| 합계 | **722** | **48** | **57,042,546** |

각 실행은 1·3·4·257·4,097·65,539·1,048,579·8,388,611바이트의 8개 크기에 3개 패턴을 사용했다. Upload→Device→Readback 전송의 **모든 payload 바이트를 CPU 참조와 비교**했다. 위 bytes는 suite의 `verified_bytes` 계수이며 추가 계약 readback/slice나 두 단계 native copy의 전체 트래픽을 합산한 값은 아니다. 성능 측정도 아니다.

CTest는 `^vulkan_transfer$` 하나를 정확히 선택했고 JUnit에서 1개 testcase의 통과와 skip/failure/error 부재를 확인했다. 설치 소비자는 별도 project의 `find_package(GraphicsSDK CONFIG REQUIRED)`로 빌드해 같은 C++ suite를 실행했다. 다른 texture/draw/depth 등의 소비자 target은 이번 실행에 포함하지 않았다.

| 계약 | 검증한 내용 |
|---|---|
| 전송·범위·상태 | 전체 bytes 및 unaligned slice, 끝 offset write, 범위 초과/overflow·빈 copy·alias·null·잘못된 memory/state 거부, 거부 뒤 유효 전송 참조 일치 |
| 거부 원자성 | copy/transition 거부를 32회 반복해 wrapper 해제와 allocation/reference 통계 원상복구 확인 |
| queue/fence | 다른 device의 queue/fence, 중복 submit, 아직 submit하지 않은 값·zero fence 거부; 완료 값 단조 증가 |
| wait | zero-time poll의 완료 또는 Timeout 허용, 이후 유한 기본 wait; 완료 뒤에도 음수·무한 sentinel `4294967295ms` 거부, `4294967294ms` 허용 |
| stale recording | CPU write 또는 sibling submit 이후 이전 recording submit 거부, 유효 sibling bytes 확인 |
| 제출 수명 | upload/device wrapper와 command list를 wait 전에 해제해도 native submission이 자원 보유; readback 일치 뒤 SDK 소유 계수 0 |
| 미제출 recording | wrapper 해제 뒤 2개 allocation·최소 8,192바이트 보유, recording 파괴 뒤 해제 |
| thread·퇴역 | 생성 thread 밖 native write 거부; public device 종료가 pending 작업 drain/retire, child description 보존·native 연산 `DeviceLost` |

zero-time poll에서 **실제 Timeout이 발생했는지는 계수하지 않았다**. 기존 suite의 완료/Timeout 허용 계약을 따르며 매 실행 Timeout 발생을 보장하지 않는다. 공개 device 종료에 따른 통제된 퇴역은 예상 밖 native device loss/TDR 검증이 아니다. SDK 소유 자원 계수는 driver 전체 메모리나 OS 메모리 회수의 측정값이 아니다. noncoherent 전용 분기 실행 여부도 별도 계측하지 않았다.

## 환경과 재현

| 항목 | 관측값 |
|---|---|
| Host | GitHub-hosted Ubuntu 24.04.5 LTS, x86_64, kernel `6.17.0-1022-azure` |
| Loader / Mesa | `libvulkan1 1.3.275.0-1build1` / `mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4` |
| Device | `llvmpipe (LLVM 20.1.2, 256 bits)`, `VK_PHYSICAL_DEVICE_TYPE_CPU` (4), software 1 / hardware 0 |
| Adapter ID | `5892941257450743403` (JSON 문자열) |
| Device / driver UUID | `6d65736132352e322e382d3075627500` / `6c6c766d706970655555494400000000` |
| Build | g++-14 / Ninja / Release, D3D12 OFF, Vulkan ON, tests ON, validation tests OFF |
| External headers | Vulkan-Headers `b379292b2ab6df5771ba9870d53cf8b2c9295daf` |
| Native SDK fingerprint | `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830` |

같은 실행에서 기존 설치 SDK device probe로 UUID·native 종류·SDK ID를 대조하고 실제 loader/driver module 경로를 읽었다. 두 버퍼 실행의 adapter 이름·ID·software flag가 그 결과와 일치한다. module·manifest의 bytes/SHA256은 receipt에 기록했으며 외부 runtime binary를 배포하지 않았다.

[`tools/run_linux_transfer.py`](../tools/run_linux_transfer.py)는 실제 Linux host와 clean source/headers checkout을 요구하고 기존 build/install/evidence directory가 있으면 거부한다. 관측된 `lvp_icd*.json` 1개를 child의 `VK_DRIVER_FILES`와 `VK_ICD_FILENAMES`에 같은 절대 경로로 지정한다. 상속 VK/VVL 입력을 제거하고 implicit layer 비활성화, 빈 explicit layer 경로, `LP_NUM_THREADS=2`, 0700 XDG runtime 경로를 사용했다. 시스템 driver/layer 파일을 교체하지 않는다.

전용 [workflow](../.github/workflows/linux-vulkan-transfer.yml)는 Ubuntu에 외부 runtime package를 준비한 뒤 아래 명령을 실행하고 결과를 artifact로 보존한다. 기존 preset·공통 C++ suite·설치 소비자 project를 사용한다.

```sh
python3 tools/run_linux_transfer.py
```

잘못된 CLI 인수 4회는 exit 1·Usage, 빈 layer 경로에서 `--validation` 요청 2회는 exit 1·`VK_LAYER_KHRONOS_validation is not installed` 진단으로 거부됐다. 이는 기대한 실패 검사다. 정상 전송은 `validation=0`이며 `validationErrorCount()==0`도 **정상 layer 실행 성공 근거가 아니다**. core/sync validation 및 오류 검출 controls 수용은 `P3-LNX-11`에 남긴다. `vulkaninfo`의 headless surface skip도 WSI 성공으로 세지 않는다.

## 증거 검증과 잔여 작업

[artifact ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-transfer/artifact.zip)의 SHA256 `82728c71e59804f62070a5eeee9201500ae92d9a28a86bf2b0e489d58b209438`은 GitHub artifact digest와 일치한다. 모든 내부 diagnostic의 크기/hash, job log와 ZIP의 receipt 일치, 검증 커밋의 runner·suite·probe·preset·workflow Git bytes, SDK 27개 소스 파일의 native fingerprint를 대조했다. 빌드/설치 static SDK library는 모두 413,918바이트, SHA256 `c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32`로 일치했다.

Windows raw SDK fingerprint `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`와 메모리 정책은 그대로다. Linux의 Git/LF bytes와 Windows의 CRLF bytes에서 나온 fingerprint 차이를 보존했다. 이전 Windows 장치 결과를 Linux 결과로 재귀속하지 않는다.

같은 커밋의 [기존 Build and correctness CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40173473105)에서는 portable C++ CTest가 통과했으나 Python 614개 중 오류 92건이 재현됐다. [별도 원본 로그](../benchmarks/reports/validation/2026-10-25-phase3-linux-transfer/common-portable-job.log)를 보존하며 기존 필수 `P3-LNX-14`는 미완료다. 전용 버퍼 CI 성공을 전체 CI 성공으로 표현하지 않는다.

이번에는 `P3-LNX-05`만 software ICD 범위로 완료한다. 물리 GPU `P3-LNX-15`는 최종 gate의 필수 의존성으로 유지한다. texture/draw/binding/depth/timestamp, 정상 validation, 전체 설치 소비자·입력/라이선스 감사 `P3-LNX-12`, Linux WSI, 경쟁 성능과 Phase 3 최종 gate는 별도 잔여다. 장시간 메모리·Intel AV 원인 조사 보류도 유지한다.
