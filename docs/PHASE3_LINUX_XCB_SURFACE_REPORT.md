# P3-WSI-02 — 공통 Surface·Linux XCB 연결

후속 [P3-WSI-03](PHASE3_LINUX_XCB_PRESENT_REPORT.md)에서 XCB swapchain/present와 BGRA8을 구현했다.
아래 caps=false/미구현 설명은 이 surface 연결 단계 당시의 기록이며 새 표시 구현의 상태와 구분한다.

2026-10-26. 공통 Surface 확장과 선택 Linux XCB 경로의 실제 native surface 생성·해제,
선택 queue·visual 지원 확인을 구현했다. 제품 구현은 `bac184b147b9bf6a8ce616b25caef8d4854896fd`,
입력/고지 감사까지 성공한 기준은 `6aa29b1bd009af009c28e0ceedebd4db1ff0d2ce`다.
이 작업은 Linux swapchain/present 구현 `P3-WSI-03`의 선행 단계다.

## 공개 계약과 구현

`SurfaceKind::Win32`의 기존 값과 `Surface{Win32, hwnd}` 초기화를 유지하고 `SurfaceKind::Xcb`,
`void* connection`, `uint32_t windowId`를 추가했다. XCB에서는 기존 `window`가 null이어야 한다.
공개 헤더에는 XCB/Vulkan/Wayland 헤더가 없다. `Device::checkSurfaceSupport(Surface)`는 생성
thread에서만 호출하며 임시 native surface와 실제 선택 graphics queue 지원을 검사한 뒤 해제한다.
추가된 virtual 함수와 Surface layout 때문에 정적 소비자는 함께 재빌드해야 한다.

Vulkan Win32는 기존 surface/queue 검사를 공통 private helper로 옮겨 기존 swapchain 경로에서도
재사용한다. D3D12의 새 확인 함수는 같은 process·생성 thread의 live Win32 HWND를 확인하며
기존 swapchain 생성 경로는 유지한다. 이 query는 swapchain format·성공한 present·모니터 출력을
보장하는 API가 아니다.

Linux는 `GFX_VULKAN_XCB=ON`에서만 private XCB header와 Vulkan XCB extension을 사용한다.
옵션은 기본 OFF이며 Linux + Vulkan이 아니면 configure에서 명시적으로 거부한다. ON에서도
display 연결은 device 생성 조건이 아니다. Core는 native window 확인 시에만 `libxcb.so.1`을
동적으로 불러오며 installed target에는 XCB hard link/include 의존성이 전파되지 않는다.

XCB connection은 caller-owned 유효 객체여야 한다. null/0 ID/혼합 필드, 존재하지 않는 창,
InputOnly 창은 InvalidArgument, 연결 오류와 다른 thread는 InvalidState다. 지원하지 않는
플랫폼/빌드·extension·procedure·queue/visual은 Unsupported다. 임의 또는 이미 해제한 connection
pointer의 유효성을 SDK가 보장하지는 않는다. 창/connection/event loop는 호출자가 계속 소유한다.

SDK는 geometry와 실제 visual을 확인한 뒤 [native XCB surface](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateXcbSurfaceKHR.html)를
생성하고, [visual 질의](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceXcbPresentationSupportKHR.html)와
[surface queue 질의](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceSupportKHR.html)를
모두 수행한다. 현재 선택된 첫 graphics family가 지원하지 않으면 다른 family/adapter로 바꾸지 않는다.
누락 extension/procedure는 headless device 생성을 막지 않는다. SURFACE_LOST는 nativeCode를
보존한 BackendFailure이며 device loss로 잘못 저장하지 않는다.

Linux `caps.swapChain=false`를 유지한다. XCB ON에서 `createSwapChain`은 먼저 surface/queue를
확인하고 실제 chain 구현이 없음을 Unsupported로 반환한다. BGRA8 형식·pipeline/transfer와
실제 acquire/submit/present는 `P3-WSI-03`, 완료·보유 수명은 `04`, resize/이벤트/표시 수용은
`05`~`08`의 잔여 작업이다. Native Wayland·XWayland·물리 Linux GPU를 수용했다고 해석하지 않는다.

## 실제 Linux 설치 소비자 검증

[전용 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40294262329)는 실제 Ubuntu 24.04에서
XCB ON/OFF SDK를 각각 build/install하고 독립 CMake 소비자를 연결했다. Xvfb 창 서버와
llvmpipe software 장치(ID `5892941257450743403`)를 사용한다. 고정 외부 Vulkan-Headers는
`b379292b2ab6df5771ba9870d53cf8b2c9295daf`다.

테스트 소유 proxy loader를 child `LD_LIBRARY_PATH`에만 적용해 실제 system loader로 전달한다.
SDK가 생성/해제한 instance/surface, 실제 native surface·visual query의 true 값과 선택 family를
기록한다. 별도 모드만 extension/procedure 누락 또는 query 결과·SURFACE_LOST를 통제한다.
실제 driver reset·native device loss·전역 loader 설정 변경은 없다.

| 새 process | 공개 surface 결과 | 생성/해제 surface | 검증 내용 |
|---|---|---:|---|
| 정상 ON | 성공 1회 | 2/2 | 성공 query와 실제 chain 미구현 거부 시 임시 surface 각각 해제; family 0·실제 visual/surface true |
| XCB extension 누락 | Unsupported | 0/0 | instance에 XCB extension 미활성; headless 유지 |
| surface queue 거부 | Unsupported | 2/2 | 실제 native true를 기록한 뒤 false로 대조; 다른 queue fallback 없음 |
| visual queue 거부 | Unsupported | 2/2 | 실제 native visual true를 기록한 뒤 false로 대조 |
| 생성 procedure 누락 | Unsupported | 0/0 | native surface 호출 없이 headless 유지 |
| SURFACE_LOST 반환 | BackendFailure / `-1000000000` | 2/2 | 원인 보존, device는 건강한 상태를 유지 |
| DISPLAY 연결 없음 | InvalidState | 0/0 | device 생성과 offscreen copy는 성공 |
| XCB OFF SDK | Unsupported | 0/0 | 같은 공개 헤더로 설치 소비자 컴파일·실행, XCB extension 미활성 |

각 process에서 임시 native instance 생성/해제는 2/2로 일치했다. 잘못된 종류/혼합 필드/0 ID,
만료 창/InputOnly와 다른 thread도 거부했고, 부적합 창은 Vulkan surface 생성에 도달하지 않았다.
SDK device를 해제한 후에도 caller의 창/connection이 살아 있음을 확인했다.
모든 8개 실행은 전후 257-byte 실제 buffer copy 전체 참조를 비교해 각 514 bytes,
합계 4112 bytes를 검증했다. 실행 중 frozen 입력은 모두 유지됐다.

이 실행은 surface/queue의 software-ICD 검증이다. Linux swapchain 생성·present·draw/texel,
Linux WSI의 필수 VVL 오류 제어 또는 사람이 보는 표시 수용을 대신하지 않는다.

## 실제 입력·배포 감사

같은 CI에서 Ninja가 실제 기록한 SDK 입력은 ON 395개, OFF 389개였다. 고정 vendor header는
ON 12개/OFF 11개로, ON만 `vulkan_xcb.h`와 system XCB header를 읽었다. 기존 video header의
고정 Git SHA를 유지하고 XCB 확장 header SHA를 별도 allowlist에 추가했다.
Core undefined-symbol 기록에는 direct `xcb_*`가 없고 installed target에는 XCB 경로/링크가 없다.
두 설치 payload는 각각 정확히 20개 파일로 공개 header·static Core·CMake 설정·LICENSE·고지를
포함한다. 고지는 설치 후 감사 도구가 추가한다. loader/ICD/libxcb binary는 payload에 넣지 않는다.
runtime 파일 해시와 libxcb의 system copyright 문서도 보존했다.

첫 전용 입력 감사는 기존 Vulkan video header를 새 도구의 vendor allowlist에 빠뜨려 실패했다.
기존 Linux 설치 감사도 새 private `Xcb.hpp`가 own allowlist에 없어 실패했다. 두 원본 로그와
당시 정책/도구를 보존했고, 검토한 header 집합을 명시한 뒤 전용 ON/OFF 감사와
[기존 설치/소비자 감사](https://github.com/blackvrice/GraphicsSDK/actions/runs/40294262269)가 통과했다.
실패를 성공으로 바꾸거나 미검토 입력을 포괄 허용하지 않았다.

## Windows 회귀와 남은 예외

Windows 전체 CTest는 **58/60 통과**다. 기존 제외는 `memory_reclamation_gpu/validation` 두 항목
그대로이며 추가 제외는 없다. 새 Win32 surface 확인·null/XCB/혼합 필드·thread 거부를 포함한
D3D12 swapchain 및 Vulkan presentation의 일반/validation 검사는 통과했다.

실패한 두 항목은 `fence_wait_gpu`, `fence_wait_validation`이다. 각각 새 프로세스의 직접
재검사와 보관된 **이전 SDK executable**에서도 `0xC0000008`로 재현됐다. 이전 executable의
embedded SDK fingerprint와 binary hash를 따로 확인했다. 원인이나 하드웨어/Intel 연관성은
확정하지 않았다. **Windows 전체 회귀 통과로 기록하지 않으며**, Phase 2 수용·Phase 3 최종
회귀의 해결을 대신하지 않는다. 이번 작업에서는 fence 구현이나 시스템 설정을 변경하지 않았다.

최초 Windows build는 새 시험 코드의 지역 변수 이름이 기존 변수와 중복돼 실패했다. 실패한
source/log를 보존하고 이름을 수정한 뒤 build/install했다. native compiler crash로 분류하지 않는다.

전체 Python은 새 판정 대조 6개를 포함해 로컬 **688개**, 같은
[Windows/Linux 기본 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40294262288)도 각각
688개, 오류·실패·건너뜀 0이다. Hosted 기본 CI 성공은 위 로컬 GPU fence 예외를 없애지 않는다.

## 정확한 source와 재현

실제 Windows SDK byte fingerprint는
`b83edc2832404cb5d116a586148d773dbf8e7a92923b66da7020abd79301e79c`,
Linux Git byte fingerprint는
`7aba5428854aa7c084b48dc56d649f2c79f14a05e82f65f46c1d0ec4c6900e46`다.
28개 SDK source와 재현 도구/시험을 포함한 43개 source 파일을 플랫폼별 ZIP으로 보존했다.
각 실행의 source·입력/배포·runtime/binary 해시는 독립적으로 기록하고 과거 GPU 결과에
새 fingerprint를 다시 붙이지 않는다. source ZIP에 외부/컴파일 binary나 Python cache는 없다.

[원본/오프라인 검증기](../benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-surface/verify_evidence.py)와
[요약](../benchmarks/reports/2026-10-26-phase3-linux-xcb-surface/summary.json)에 연결한다.
저장소 루트에서 `python benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-surface/verify_evidence.py`로
원본 해시·두 source ZIP·실제 8개 native 결과·ON/OFF 감사·688개 영수증과 보존된 실패를 검사한다.
GPU나 네트워크는 필요하지 않다.

실제 재실행은 [.github/workflows/linux-xcb-surface.yml](../.github/workflows/linux-xcb-surface.yml)의
고정 headers, Linux 의존성, fresh ON/OFF build/install/consumer 명령을 따른다. 기본 OFF와 ON의
SDK source fingerprint는 같으므로 실제 CMake cache·binary hash·native 관측을 함께 구분해야 한다.
고정 경로가 기존에 있으면 덮어쓰지 말고 별도 fresh checkout/output을 사용한다.
