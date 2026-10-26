# P3-WSI-04 — XCB 출력 자원 완료·보유 수명

2026-10-26. 제품의 기존 backbuffer·recording·submission 보유와 present 완료 계약을
실제 Linux XCB 설치 소비자로 검증한다. 시험 구현 기준은
`c7d8d98011fb59def0b519f67e37bcfd4da5e283`이다. 이 단계에서는 SDK 제품 소스를 변경하지 않았다.
일반 구성과 고정 core/sync validation 구성이 모두 통과했다. SDK 제품 수정 없이 기존 수명
계약의 실제 실행·오류 controls와 반복 가능한 검증 경로를 보강했다.

## 계약과 실제 검사

| 경우 | 관측·거부 기준 |
|---|---|
| 보유 wrapper·recording | TexturePtr가 남으면 같은 크기의 resize도 InvalidState. wrapper 해제 뒤 recording만 남아도 거부하며 native 재생성을 호출하지 않음. 모두 해제하면 동일 extent 재생성 성공 |
| present 이전 명령 | present 이후 새 texture 사용과 이전 recording 제출 거부. 8회 acquire/present 순환 뒤에도 이전 recording은 제출 불가 |
| chain 파괴 후 보유 | 공개 chain을 파괴해도 남은 image/recording이 native chain을 보유. native texture 연산·제출은 InvalidState이며 CPU description/footprint는 유지 |
| 실제 미완료 제출 | native event로 GPU 실행을 대기시켜 실제 submission fence의 NOT_READY 확인. 공개 recording·image를 해제해도 pending submission이 보유. chain destructor의 실제 DeviceWaitIdle 진입 후 event를 해제하고, 완료 뒤 자원 회수와 전체 readback 대조 |
| present 완료 | native present 진입에서 event를 해제. 반환 전에 maintenance1 present fence의 실제 성공 wait를 관측. 렌더링 public recording을 먼저 해제해도 readback 유지 |
| 부분 생성·present 전 실패 | 두 번째 image view 생성, present completion fence 생성, present signal submit 직전에 합성 OutOfMemory 반환. 부분 view/semaphore/chain 회수, native present 미호출, acquired image 유지와 정상 프레임 재시도 검증 |
| 다른 장치 | backbuffer를 다른 device의 command list에 입력하면 WrongDevice |
| 장치 종료·퇴역 | 실제 미완료 제출 상태에서 device를 파괴. GPU drain과 native 자원 회수를 확인하고, 남은 chain·command·buffer 연산은 DeviceLost, texture CPU 메타데이터는 유지 |

같은 크기 재생성은 보유 참조 거부를 분리하기 위한 대조다. 실제 창 크기 변화·실패한 native
재생성·out-of-date/suboptimal 수용인 WSI-05를 대신하지 않는다. 성공한 present 이전의 recording은
버전 변경으로 무효화되지만, TexturePtr 자체는 그 native image가 다시 current가 되면 새 명령에
사용할 수 있다는 기존 공개 계약을 유지한다.

## 테스트 관찰자와 대기

설치한 `GraphicsSDK::Core`만 링크하는 외부 소비자, Xvfb의 실제 X 서버와 llvmpipe software ICD를
사용한다. 전용 proxy는 실제 system Vulkan loader로 전달한다. 일반 surface/present 시험에서
사용하던 proxy에 compile definition을 지정한 전용 빌드만 수명 관찰자를 포함한다.

GPU 대기는 실제 VkEvent와 vkCmdWaitEvents를 앞선 command buffer로 제출해 만든다. SDK에
반환하는 GetFenceStatus를 NOT_READY로 위조하지 않으며 실제 native fence를 별도로 조회한다.
보조 thread는 지정한 native API 경계 진입을 관측하고 vkSetEvent로 해제한다. 경계가 누락되면
최대 10초 뒤에도 event를 해제해 실패 시 대기 작업이 남지 않도록 한다. release 호출과 event
파괴를 직렬화하고 완료 뒤 전용 command pool/event도 파괴한다.
host signal을 포함하는 HOST source stage와 이후 명령을 막는 ALL_COMMANDS destination은
[Khronos의 event 대기 계약](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWaitEvents.html)에 따른다.

이 대기 장치는 software adapter 한 개가 확인된 테스트 프로세스에서만 실행한다. 물리 GPU,
driver reset·TDR·hot-unplug를 사용하지 않으며 공개 SDK에 fault/gate API를 추가하지 않는다.
합성 OutOfMemory는 해당 native 호출을 하기 전에 반환하는 시험 control이며 실제 driver의
메모리 고갈을 관측한 결과가 아니다. 정상 native 성공 결과와 GPU 완료를 합성하지 않는다.

관찰자는 성공한 present fence wait, native view/semaphore/fence의 생성·파괴, view보다 먼저
chain을 파괴하거나 미완료 작업을 보유한 채 chain을 파괴하는 순서 오류를 검사한다.
장치·instance·surface·chain의 native 생성/파괴도 모두 일치해야 통과한다.

## 참조와 검증 범위

한 구성에서 6개 경우·16개 거부·3개 gate·실제 미완료 fence 2개·합성 실패 3개를 검사한다.
실제 present 11회 모두 completion fence 연결과 성공 wait가 필요하다. BGRA8 65×7 readback
3개는 매 texel과 모든 padding을 독립 대조하며, 비교 범위는 10752 bytes다.
현재 software 조합의 실제 view/semaphore는 각각 22개, fence는 31개이며 모두 회수된다.
chain 8개·surface 7개·device 3개·instance 4개도 생성과 파괴 수가 같아야 한다.

validation은 고정 VVL 1.3.290, XCB profile과 기존 dependency lock으로 빌드한다. 빈 settings,
외부 override 제거, 실제 로딩된 layer 경로와 binary/manifest 해시를 보존한다. SDK error
counter와 장치 파괴 이후까지 전달된 stderr 오류를 확인한다. 시험용 event/pool은 SDK 자원
통계와 별개로 실제 해제되며, 총 수치에 test gate를 제품 자원으로 섞지 않는다.

이번 결과는 이 수명 경로의 software-ICD 검증이다. 실제 Linux 물리 GPU, 사람이 보는 출력,
모든 resize·창 소실·다중 창 및 validation 검출 controls는 각각 LNX-15와 WSI-05~08에 남긴다.
장시간 메모리 증가·Intel 충돌 원인이나 전체 Phase 3 완료로 해석하지 않는다.

## source와 재현

SDK 28개 파일의 byte fingerprint는 기존 WSI-03 최종 제품과 같다.
Windows: `7182157752bc2c8ae39e5fc6eb0bc0b829f3f6db5cb1e2a1ae82bce5fdc7e3f8`.
Linux Git: `64d4d3d994a3e987e1f0f541f310ed8404ab51c2e9b785b5a29c49ed607defc2`.
서로 다른 checkout 바이트를 하나의 해시로 바꾸지 않는다.

필수 Linux 입력과 고정 headers는 [수명 workflow](../.github/workflows/linux-xcb-lifetime.yml)를 따른다.
fresh SDK build/install 뒤 [`tests/linux_xcb_lifetime`](../tests/linux_xcb_lifetime/CMakeLists.txt)를
외부 소비자로 빌드하고, [`run_linux_xcb_lifetime.py`](../tools/run_linux_xcb_lifetime.py)에 실제
binary·proxy·정적 Core·ICD와 선택 validation 경로를 전달한다. 매번 새 출력 경로를 사용한다.
binary·테스트 소스·설치 Core·선택 layer의 해시는 실행 전후 같아야 한다. 컴파일 binary와 외부
runtime은 source ZIP/저장소에 배포하지 않는다.

Windows에서는 같은 SDK fingerprint를 내장한 기존 executable로 `vulkan_presentation`과
`vulkan_presentation_validation` 2개를 다시 실행해 2/2 통과했다. 실제 사용하는 depth fixture도
포함해 frozen input과 전체 LastTest를 보존한다. Windows 전체 63/65 및 기존 fence 예외 2개는
이전 WSI-03의 결과이며 이번에 전체 65개를 다시 실행한 것으로 표시하지 않는다.

전체 Python 검사는 새 판정 검사 6개를 포함해 703개다. 처음 실행은 703개 unittest 자체는
통과했지만 커밋 도중 tracked 입력 목록이 바뀌어 캠페인이 불합격했다. 원본을 보존하고 commit을
고정한 새 경로에서 전체를 다시 실행해 입력 불변과 703개 통과를 확인했다. Linux/Windows CI도
동일한 703개를 통과했다. 이전 불합격 영수증을 성공으로 바꾸지 않는다.

## 완료 근거

[일반·고정 validation 수명 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40319144654)와
[Linux/Windows 기본 CI·전체 Python](https://github.com/blackvrice/GraphicsSDK/actions/runs/40319144594)는
같은 시험 커밋 `c7d8d98011fb59def0b519f67e37bcfd4da5e283`을 사용했다. 해당 커밋의 7개 workflow가
모두 성공했다. 기본 CI와 로컬 Windows 실제 표시 검사는 별도 결과다.

[요약 영수증](../benchmarks/reports/2026-10-26-phase3-linux-xcb-lifetime/summary.json),
[원본 해시 목록](../benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-lifetime/inventory.json),
[오프라인 검증기](../benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-lifetime/verify_evidence.py)를 제공한다.
source ZIP 2개는 각각 SDK 28개를 포함한 재현 입력 52개와 source receipt를 담는다. Linux와
Windows checkout 바이트를 구분하며 compiled binary, 외부 runtime, Python cache는 포함하지 않는다.

저장소 루트에서 다음 명령으로 GPU·네트워크 없이 소스, raw artifact, 실제 결과와 보존한
입력 변경 불합격을 재검증한다. 새 native 실행을 수행하는 명령은 아니다.

```sh
python -B benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-lifetime/verify_evidence.py
```
