# P3-WSI-03 — Linux XCB swapchain·BGRA8

후속 [P3-WSI-04](PHASE3_LINUX_XCB_LIFETIME_REPORT.md)는 실제 미완료 제출·보유 자원·종료·부분 실패
수명을 별도 검증한다. 아래 WSI-04 미완료 표기는 이 표시 구현 단계 당시의 수용 경계다.

2026-10-26. `GFX_VULKAN_XCB=ON`에서 실제 XCB swapchain acquire→submit→present를 연결하고
`Format::BGRA8UNorm`을 추가했다. 최초 제품 구현은 `1a378fcb88d0c093d3e84ea01e4a2194de565d25`다.
이후 시험의 기존 alpha/footprint 계약과 진단 로그 수집을 수정했다. 표시 모드별 이미지 한도 수정은
`abd3e361c89f36fef35e7902d9801ac667217ffd`이며 이전 제품 source와 실행 영수증을 별도 보존한다.

## 구현 계약

기존 enum RGBA8=0/RGBA16F=1/D32=2를 유지하고 BGRA8=3을 덧붙였다. BGRA8은 4-byte texel,
메모리 순서 B,G,R,A다. clear와 shader 출력의 논리 채널은 R,G,B,A이며 native UNORM 형식이
저장 순서를 처리한다. CPU swizzle이나 SRGB 변환은 수행하지 않는다.
Vulkan texture/view·render-pass attachment·pipeline·format-feature query의 기존 두 형식 분기를
명시적 native format 매핑으로 바꿔 BGRA가 RGBA16F로 흘러가지 않게 했다. D3D12도 실제
`DXGI_FORMAT_B8G8R8A8_UNORM`을 사용한다. 알 수 없는 enum은 기존 validation이 거부한다.

XCB 표면은 RGBA8 UNORM을 우선하고 없으면 BGRA8 UNORM을 선택한다. 색 공간은 SRGB nonlinear이며
SRGB native format만 제공되면 Unsupported다. Win32 표시의 RGBA8 정책은 유지한다.
선택 graphics queue·surface/visual·maintenance1 extension/feature가 충족돼야 `caps.swapChain=true`다.
기본 XCB OFF는 headless와 caps=false를 유지하고, ON에서도 device 생성은 DISPLAY 연결을 요구하지 않는다.

caller-owned connection/window를 보관하고 live geometry와 요청 extent를 맞춘다. 요청 image count는
2..4개이며 실제 surface min/max와 선택 present-mode의 min/max를 모두 적용한다. `vsync=true`는 FIFO, false는 IMMEDIATE 우선·MAILBOX
차선이다. 둘 다 없으면 Unsupported이며 FIFO로 조용히 대체하지 않는다. backbuffer의 public format,
extent와 footprint는 실제 native swapchain image에 맞춘다.

기존 Win32의 공통 host-fenced acquire/present를 XCB에서도 재사용한다. native image별 semaphore와
maintenance1 present fence를 사용하고 완료를 기다린다. [Khronos의 자원 수명 설명](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)에 따라
submit fence 또는 WaitIdle만으로 마지막 present의 자원 해제 가능 시점을 대신하지 않는다.
이 fence는 모니터 scanout·사람의 출력 확인을 의미하지 않는다.
실제 선택 목록은 [surface format 질의](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceFormatsKHR.html)에 따른다.

## 실제 참조와 native 관측

전용 Linux CI는 실제 Xvfb X 서버와 llvmpipe software ICD, 설치 SDK만 참조하는 외부 CMake
소비자를 사용한다. 자체 proxy는 실제 system Vulkan loader로 전달하며 native swapchain 설정,
성공 acquire/present, present fence 연결과 성공한 native fence wait를 관측한다.
정상 case는 driver 결과를 합성하지 않는다. 별도 거부/선택 case만 형식 목록 또는 present-mode
목록을 제한하며 실제 FIFO/MAILBOX를 사용한다. driver reset이나 전역 loader 설정 변경은 없다.

| process | 검사 | swapchain / 실제 present | 전체 이미지 |
|---|---|---:|---:|
| 정상 | 요청 buffer 2/3/4 × vsync OFF/ON, 65×7·17×9 | 6 / 18 | 24 |
| SRGB-only 형식 대조 | UNORM 없이 SRGB를 성공으로 취급하지 않음 | 0 / 0 | 6 offscreen |
| FIFO-only 대조 | vsync=false 거부 후 FIFO 실제 실행 | 1 / 3 | 9 |
| IMMEDIATE 부재 대조 | MAILBOX 실제 선택 | 1 / 3 | 9 |

각 process는 RGBA8/BGRA8 offscreen 전송·clear·scissor draw·alpha blend 참조를 먼저 수행한다.
각 창 프레임도 전체 texel과 존재하는 각 행 padding sentinel을 독립 대조한다. asymmetric clear와
빨강 draw를 사용해 R/B byte 순서를 구별하며 alpha blend의 기존 source-alpha 대입 계약도 비교한다.
요청 extent 불일치는 native swapchain 생성 전에 거부한다. public format과 native format은 모두
BGRA8 UNORM인 44임을 해당 software 조합에서 관측한다. 실제 native image count는 요청값과
surface 최소값 이상이며 요청값과 같다고 가정하지 않는다.
수정 후 software MAILBOX에서는 요청 2개에 모드별 최소 4개를 적용해 실제 4개가 생성됨을 관측했다.

한 실행 구성의 합계는 swapchain 8개·실제 present/acquire/완료 fence wait 각각 24회,
전체 프레임 참조 캡처 48개·texel bytes 72864·전송 참조 bytes 14560·padding 행 360개다.
이미지 수는 참조 캡처 횟수이며 native image 객체의 고유 개수가 아니다.
모든 관측 instance/device/surface/swapchain 생성·해제 계수는 일치한다.
일반 구성과 고정 validation 구성은 독립 process/raw receipt로 보존한다.

## 검증 runtime과 범위

XCB validation은 고정 VVL 1.3.290 및 기존 dependency lock을 그대로 사용하되
`tools/prepare_linux_validation.py --xcb`와 별도 profile에서 `BUILD_WSI_XCB_SUPPORT=ON`으로
빌드한다. 기존 headless profile·출력 경로와 구분한다. 외부 runtime은 시스템에 설치하거나 SDK에
배포하지 않는다. `DeviceDesc.validation=true`, core/sync/submit-time 설정, 빈 settings 경로,
VK/VVL override 제거와 실제 로딩된 layer 경로·binary/manifest 해시를 검사한다.

이는 이 단계의 acquire/submit/present와 참조에 대한 검증이다. `P3-WSI-04`의 보유 backbuffer,
미완료 제출·퇴역/파괴/실패 수명 전체 controls, `05`~`06`의 resize/out-of-date·숨김/닫기/소실,
`07`의 전체 다중 창/오류 controls·최종 validation matrix는 완료 처리하지 않는다.
실제 Linux 물리 GPU `P3-LNX-15`, XWayland/Wayland, 사람이 보는 표시 `08`도 별도다.

## Windows 회귀·보존한 실패

새 BGRA 검사 5개는 Vulkan 일반/validation, D3D12 hardware 일반/validation, WARP에서 모두 통과했다.
각 장치에서 RGBA8/BGRA8의 전송 2회와 clear/draw/blend 전체 이미지 6개를 확인했다.
최종 전체 CTest는 **63/65 통과**다. 기존 제외인 `memory_reclamation_gpu/validation` 두 항목은
유지했고 추가 제외는 없다. `fence_wait_gpu/validation` 예외 두 개는 이전 단계의 동일한
기존 제한으로 분리 보존하며 전체 Windows GPU 회귀 성공으로 바꾸지 않는다.

처음 추가한 시험은 alpha를 1로 기대하고 D3D12의 마지막 행 padding까지 항상 존재한다고
가정해 실패했다. 기존 구현을 변경하지 않고 올바른 CPU 참조·범위로 수정했다. 다음에는 validation을
요청하지 않은 D3D12의 정상 Unsupported 응답을 0 계수로 기대한 시험을 고쳤다.
그 원본 source·실패 로그와 수정 후 BGRA 5/5 및 전체 63/65를 각각 보존했다.

첫 Linux validation renderer는 종료 코드 0과 오류 계수 0을 반환했지만, `LD_DEBUG`를 창 서버
도구까지 적용해 진단 문자가 JSON에 섞이면서 수집기가 실패했다. 원본을 보존하고 진단 설정을
native renderer에만 적용해 엄격한 JSON 파싱과 실제 layer 로딩 확인을 유지했다. 실패 영수증을
성공으로 바꾸지 않는다. 래퍼의 stderr 병합까지 실제 재현하는 검사를 추가했으며, native stderr를
별도 파일로 직접 받아 JSON stdout과 분리한다.

이후 validation은 MAILBOX에서 `VUID-VkSwapchainCreateInfoKHR-presentMode-02839`를 보고했다.
일반 surface 질의의 최소값은 3이지만 선택 MAILBOX의 최소값은 4였다. 관찰 proxy가 기존 SDK
callback을 그대로 호출하면서 오류 메시지도 stderr에 기록하도록 해 원문을 확보했다.
SDK는 `VkSurfacePresentModeEXT`를 포함한 `vkGetPhysicalDeviceSurfaceCapabilities2KHR`로 실제
선택 모드의 한도를 다시 조회하고, 일반 한도와 함께 적용한다. native 생성에는 해당 모드 하나를
`VkSwapchainPresentModesCreateInfoEXT`로 명시한다. 한도를 4로 하드코딩하거나 validation을 끄지 않는다.
[Khronos의 모드별 질의 계약](https://docs.vulkan.org/refpages/latest/refpages/source/VkSurfacePresentModeKHR.html)에
따른 수정이며 원래 실패를 성공으로 재표기하지 않는다. stricter MAILBOX 최소/최대값 거부 회귀를
추가해 전체 Python 판정 검사는 새 9개를 포함해 **697개**다.

## 재현과 source

[전용 workflow](../.github/workflows/linux-xcb-present.yml)와 설치 소비자
[`tests/linux_xcb_present`](../tests/linux_xcb_present/CMakeLists.txt)를 따른다. 필수 Linux 입력은
g++-14, CMake/Ninja, pkg-config·libxcb1-dev, Vulkan loader·Mesa software ICD, Xvfb/xauth와
고정 Vulkan-Headers `b379292b2ab6df5771ba9870d53cf8b2c9295daf`다.

```sh
cmake -S . -B out/present-sdk -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14 -DGFX_ENABLE_D3D12=OFF -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_XCB=ON -DGFX_BUILD_TESTS=OFF -DGFX_BUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX="$PWD/out/present-install"
cmake --build out/present-sdk --parallel 1 && cmake --install out/present-sdk
cmake -S tests/linux_xcb_present -B out/present-consumer -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14 -DCMAKE_PREFIX_PATH="$PWD/out/present-install"
cmake --build out/present-consumer --parallel 1
```

runner의 실제 ICD 경로와 validation runtime 경로는 workflow처럼 관측해 전달하고 매번 새 출력
경로를 사용한다. source byte fingerprint는 Windows 실제 실행
`7182157752bc2c8ae39e5fc6eb0bc0b829f3f6db5cb1e2a1ae82bce5fdc7e3f8`,
Linux Git 실행 `64d4d3d994a3e987e1f0f541f310ed8404ab51c2e9b785b5a29c49ed607defc2`이다.
모드별 질의 수정 이전 Windows `46716b13582a5cbd1d18ab165bf291ab0fc4ce1d96e2dfda1932040b81513307`와
Linux `e2e5c0d39f8a53262f193979f5eb533ea385337f0e3969c28649c7615d197446` 결과도 원래 identity로 보존한다.
컴파일·runtime binary는 식별 해시만 보존하고 source ZIP/저장소에 포함하지 않는다.
소스와 raw 결과의 정확한 연결은 완료 영수증·검증기에 기록한다.

## 완료 근거

최종 제품·시험 기준은 `abd3e361c89f36fef35e7902d9801ac667217ffd`다.
[실제 Linux 일반/validation 실행](https://github.com/blackvrice/GraphicsSDK/actions/runs/40309705431)과
[Linux/Windows 기본 CI·Python 697](https://github.com/blackvrice/GraphicsSDK/actions/runs/40309705771)를
원본 artifact ZIP과 해시로 보존했다. Windows의 로컬 전체 native 회귀 63/65는 기본 CI와 별도다.

[요약 영수증](../benchmarks/reports/2026-10-26-phase3-linux-xcb-present/summary.json),
[원본 해시 목록](../benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-present/inventory.json),
[오프라인 검증기](../benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-present/verify_evidence.py)를 제공한다.
source ZIP 6개는 각각 SDK 28개를 포함한 재현 입력 50개와 source receipt를 담는다.
최초 실패, 진단 분리 실패, MAILBOX 한도 실패·진단, 수정 전후 Windows 결과는 원래 해시로 구분한다.
전체 native 자원 수명·resize·소실·다중 창과 사람의 표시 검사는 WSI-04~08에 남는다.

저장소 루트에서 다음 명령으로 GPU·네트워크 없이 원본 해시, ZIP, native 결과, validation runtime,
전체 프레임 참조, 보존한 실패와 Python 영수증을 재검증한다. 새 native 실행을 수행하는 명령은 아니다.

```sh
python -B benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-present/verify_evidence.py
```
