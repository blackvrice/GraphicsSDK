# Phase 3 — Linux 실행 환경 조사

2026-10-25 · 작업 `P3-LNX-01` · 기준 커밋 `b18505d`.

**환경 조사는 완료했으며, 현재 조사 범위에서는 사용할 Linux 실행 환경을 확인하지 못했다.** WSL 상태 조회는 설치되지 않았다는 오류를 반환했다. Linux OS·compiler·Vulkan loader/ICD·GPU·validation layer는 관측하지 못했으므로 “미관측”으로 기록한다. 해당 Linux 구성요소가 실제로 설치되지 않았다고 추정하거나 Linux 빌드·런타임을 통과 처리하지 않는다.

제품 SDK fingerprint `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`는 그대로다. 배포판/VM/컨테이너 설치·시작, Windows 기능·드라이버·시스템 설정 변경, SSH 접속, GPU 작업은 수행하지 않았다. 조사 결과 파일만 추가했다.

## 실제 관측

현재 host는 Windows `10.0.26220` build `26220`, 64-bit다. 조사 범위는 host PATH의 지정 실행 파일, WSL 목록/상태, 선택 서비스 이름, SSH config의 존재 여부다. 전체 디스크·네트워크·원격 계정이나 인증 파일을 탐색한 결과가 아니다.

| 후보/항목 | 관측 결과 | 해석 |
|---|---|---|
| `wsl.exe` | System32 실행 파일 존재 | 실행 파일의 존재만으로 WSL 설치나 Linux 배포판 가용성을 뜻하지 않음 |
| `wsl --list --verbose` | exit **1**, stderr에 WSL 미설치 메시지 | 정상 배포판 목록이 아님. Linux shell을 실행하지 않음 |
| `wsl --status` | exit **50**, stderr에 같은 미설치 메시지 | 실패를 성공이나 “설치된 배포판 0개”로 바꾸지 않음 |
| Docker/Podman | 지정 `.exe`를 PATH에서 찾지 못함 | 이 세션에서 실행할 CLI를 확인하지 못함. 시스템 전체 미설치 단정 아님 |
| VirtualBox/vmrun/Multipass/QEMU | 지정 `.exe`를 PATH에서 찾지 못함 | 다른 위치나 외부 VM의 존재를 부정하는 결과 아님 |
| WslService/LxssManager/vmms/com.docker.service | 조회한 이름의 서비스 결과 없음 | 조회 범위의 보조 관측. Windows 기능 설치/활성화를 시도하지 않음 |
| SSH | Windows OpenSSH client 존재, 사용자 `.ssh/config` 없음 | 지정된 원격 Linux 대상 미확인. SSH key·known_hosts를 읽거나 원격 host를 추측해 접속하지 않음 |
| WSL/Docker/Podman 관련 환경 키 | 조회한 패턴의 키 없음 | 다른 설정 방식이나 외부 환경의 부재를 보증하지 않음 |
| CMake / vulkaninfo | **Windows** 실행 파일 존재 | Linux compiler/loader/ICD/GPU 가용성 증거로 사용하지 않음. Windows vulkaninfo는 이번에 실행하지 않음 |
| Ninja/g++/clang++ | 지정 `.exe`를 PATH에서 찾지 못함 | Windows PATH 관측이며 Linux toolchain 유무를 판단한 것이 아님 |

[관측 요약과 해시](../benchmarks/reports/2026-10-25-phase3-linux-environment/summary.json), [host inventory 원본](../benchmarks/reports/validation/2026-10-25-phase3-linux-environment/run/host-inventory.stdout), [전체 명령·해독된 stdout/stderr](../benchmarks/reports/validation/2026-10-25-phase3-linux-environment/run/commands.json)에 연결한다. WSL의 원본 stderr는 UTF-16LE 바이트 그대로 보존하며 commands.json에 해독한 문자열도 기록한다. 두 nonzero 코드는 환경 부재에 관한 관측이며 Linux 검증 성공 코드가 아니다.

## 가능한 작업과 현재 한계

| 범위 | 현재 가능 여부 | 관련 작업 |
|---|---|---|
| 저장소의 Linux 빌드 설정 검토·preset 작성 준비 | Windows에서 소스 편집 가능 | `P3-LNX-02`. 실제 Linux configure 검증과 분리 |
| Linux native configure/compile/install | 실행 환경 미확인으로 이번에 시도하지 않음 | `P3-LNX-02`, `P3-LNX-03`, `P3-LNX-12` 미완료 |
| `libvulkan.so.1`, Linux ICD·adapter/device 생성 | loader/ICD/GPU 모두 미관측 | `P3-LNX-04` 미완료 |
| Linux 전송·이미지·draw·수명·timestamp | 실제 native 결과 없음 | `P3-LNX-05`~`10` 미완료 |
| Linux core/sync validation | Khronos layer·설정·오류 controls 미관측 | `P3-LNX-11` 미완료. layer JSON 존재만으로 실행 성공 처리 금지 |
| Linux WSI·화면·동일 환경 성능 비교 | display server·surface·GPU 경로 미관측 | `P3-WSI-01`~`08`, `P3-LBEN-01`~`04` 미완료 |
| 현재 CI 설정 | ubuntu-24.04/g++-14/Ninja의 **portable-core** job만 존재 | `.github/workflows/build.yml`은 설정 파일 근거. 이번 원격 CI 실행/성공이나 Vulkan ON 증거가 아니며 `P3-LNX-13`은 미완료 |

현재 `portable-core` preset은 D3D12 OFF지만 Vulkan ON을 지정하지 않는다. 기본 `GFX_ENABLE_VULKAN=OFF`라서 Linux Vulkan 검증을 대신할 수 없다. 전용 Vulkan ON preset, C++23 `std::expected`를 지원하는 실제 compiler, CMake/Ninja와 외부 Vulkan-Headers 경로는 `P3-LNX-02`에서 구성·검증한다. Windows에 보존된 헤더/빌드/validation runtime도 Linux binary 실행 근거로 옮겨 세지 않는다.

## 다음 실행 환경에 필요한 receipt

Linux 환경이 마련되면 아래 항목을 **그 환경 안에서** 관측해 새 receipt를 추가한다. 기존 Windows/WSL 부재 기록은 덮어쓰지 않는다. WSL/VM/컨테이너가 존재하더라도 실제 Linux compiler와 Vulkan ICD·물리/가상/software GPU를 각각 확인해야 한다.

| 분류 | 기록할 항목 | 수용 경계 |
|---|---|---|
| OS/실행 위치 | distro/version, kernel, architecture, native/WSL/VM/container 구분 | Windows host 버전으로 대체하지 않음 |
| Build | 실제 compiler/version, C++23 expected configure/compile, CMake≥3.25, generator, headers의 고정 SHA·Linux 경로 | 명령 존재와 실제 SDK compile을 구분 |
| Loader/ICD | `libvulkan.so.1` 해석 경로, 실제 사용 ICD manifest/library, 영향 있는 loader 환경/설정 | 설치 파일 목록만으로 device 생성 성공 처리 금지 |
| GPU | Vulkan 장치 이름·vendor/device·driver·native ID, queue, hardware/software/가상화 구분 | software 결과를 실제 GPU 성능·장치 수용으로 합산하지 않음 |
| Validation | Linux Khronos layer library/manifest/version·실제 로딩, core/sync 설정·오류 검출 controls | Windows DLL 또는 ERROR 0만으로 대체하지 않음 |
| WSI | 선택 X11/Wayland·display/session·surface/queue/완료 보장 | headless 성공이 창 출력·사람의 표시 확인을 대신하지 않음 |

실제 Linux shell에서 시작할 읽기 전용 조사 예시는 다음과 같다. 명령 부재·nonzero·권한 오류도 원본으로 남긴다. 이 예시들은 **이번 Windows host에서 실행하지 않았다**.

```sh
uname -srmo
cat /etc/os-release
command -v c++ g++ clang++ cmake ninja vulkaninfo
c++ --version
cmake --version
ninja --version
ldconfig -p
ls /dev/dri
ls /usr/share/vulkan/icd.d /etc/vulkan/icd.d
ls /usr/share/vulkan/explicit_layer.d /etc/vulkan/explicit_layer.d
vulkaninfo --summary
```

manifest 검색 경로는 예시이며 실제 loader 환경 변수·사용자 경로·실행 결과까지 확인한다. `vulkaninfo` 성공도 SDK 계약 suite의 성공은 아니다. SDK configure/compile·장치 생성·readback·validation은 각각 다음 ID에서 실제로 검사한다.

이 작업의 [수집 runner](../benchmarks/reports/validation/2026-10-25-phase3-linux-environment/run.py)는 Windows 측 관측용이며 동일 결과 폴더가 있으면 덮어쓰지 않고 실패한다. 새로운 Linux 환경의 관측 도구나 지원 판정기로 오해하지 않는다.

`P3-LNX-01`만 “가용 환경 조사 완료”로 체크한다. `P3-LNX-02`의 선행 ID가 충족돼도 실제 Linux compiler/CMake 환경이 확인됐다는 뜻은 아니다. 현재 `next_order`의 다음 자동 권장 작업은 Windows에서 진행 가능한 필수 `P3-CAP-01`(Vulkan debug marker)이다. 보류된 장시간 메모리·Intel AV 조사와 Phase 3 최종 수용은 그대로 남긴다.
