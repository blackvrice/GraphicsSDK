# P3-WSI-01 — Linux 창 시스템·동기화 범위

2026-10-26. 첫 Linux 표시 경로는 **X11/XCB**로 결정한다. Native Wayland는 초기 구현에서
미지원으로 명시한다. XWayland는 XCB 경로로 연결 가능하더라도 별도 검증 전에는 지원 조합에
넣지 않는다. 이 결정은 Linux 표시 요구를 제외하지 않는다. 아래 필수 XCB 구현·표시 수용
`P3-WSI-02`~`08`은 그대로 남는다.

기존 headless Linux Vulkan과 Windows Win32 표시 계약을 보존하면서, 창 서버와 창을
애플리케이션이 제공하는 작은 초기 경로를 추가한다. XCB 우선 순위는 현재 SDK의 단일 queue와
호출자 소유 native handle 모델에 맞고, 독립 X 서버를 이용해 표시 사전 조건과 오류를 CI에서
재현할 수 있기 때문이다. Wayland compositor의 surface 역할·configure/ack·event dispatch와
수명 규약은 별도 구현이 필요하므로 XCB pointer로 위장하거나 암묵적으로 전환하지 않는다.

## 범위와 미지원 조합

| 조합 | 초기 결정 | 확인/수용 계획 |
|---|---|---|
| Native X11 + XCB, 같은 graphics/present queue, maintenance1 완료 fence | 필수 첫 구현 경로 | `02` native handle·surface/queue, `03` swapchain/형식, `04` completion/수명, `05` resize, `06` 이벤트/소실, `07` GPU/validation, `08` 실제 사람의 표시 확인 |
| Xvfb + software ICD | 자동 사전 조건·이후 회귀용 | 물리 GPU·실제 모니터 수용과 분리. 이번 probe는 창과 native surface/query/device 생성까지만 수행 |
| XWayland + XCB | 별도 실행 근거 전까지 미지원 조합 | XWayland/compositor/version·실제 연결·queue·동작·화면 확인을 추가한 뒤 지원표 확장 |
| Native Wayland (`wl_display*`, `wl_surface*`) | 초기 subset 미지원 | 향후 native handle·xdg-shell 역할/configure·event dispatch·완료/resize/표시 검증을 별도로 설계. extension 열거만으로 지원 판정 금지 |
| Xlib `Display*`/Window | 직접 API 미지원 | XCB connection/window를 호출자가 명시 제공해야 한다. Xlib Display를 XCB pointer로 캐스팅하지 않음 |
| 그래픽 queue와 표시 queue가 분리됨 | 초기 subset 미지원 | 현재 첫 graphics family를 유지한다. 별도 present queue·ownership transfer를 임의로 추가하거나 다른 adapter로 fallback하지 않음 |
| swapchain/maintenance1 extension 또는 feature 누락 | 해당 표시 조합 Unsupported | offscreen 동작은 유지. QueueWaitIdle만으로 마지막 present 정리를 안전하다고 대체하지 않음 |
| RGBA8/BGRA8 UNORM + SRGB nonlinear·필수 usage 누락 | 해당 표시 조합 Unsupported | native BGRA를 RGBA로 잘못 설명하거나 implicit swizzle·색 변환을 하지 않음 |
| 창 서버 연결 없음·만료 창·잘못된 종류/handle | 명시적 오류 | 창 서버 자동 시작·추측·fallback 없이 실패. 아래 native no-display control과 후속 SDK 오류 검증을 구분 |

[Khronos XCB surface](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_xcb_surface.html)는
XCB connection/window에서 Vulkan surface를 만들고 visual의 표시 지원을 조회하는 API를 제공한다.
[Wayland surface](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_wayland_surface.html)는
별도의 display/surface와 compositor 표시 질의를 사용한다. 두 native 종류의 수명과 식별자는
호환된다고 가정하지 않는다.

## 공개 handle·소유권 결정 (`P3-WSI-02` 구현 입력)

현재 `SurfaceKind`는 Win32만 있고 Linux SDK `caps.swapChain=false`다. **이번에는 공개 헤더나
SDK C++를 변경하지 않는다.** 다음 단계에서 `SurfaceKind::Xcb`와 불투명 connection, 32-bit
XCB window ID를 명시 전달하는 필드를 설계·추가한다. 기존 `Surface{Win32, hwnd}` 초기화와
Win32 값은 보존하고 확장된 struct를 쓰는 정적 소비자는 재빌드한다. 실제 필드명·layout 변경과
컴파일/설치 소비자 검증은 `P3-WSI-02`에서 수행한다.

공개 헤더에는 XCB/Wayland/Vulkan 헤더를 노출하지 않는다. HWND와 XCB 숫자 ID를 같은
`void* window`에 암묵적으로 섞지 않는다. caller는 connection과 window를 만들고, SDK의
swapchain/backbuffer/recording·native surface 정리가 끝날 때까지 살아 있게 유지한다.
SDK가 제공된 창·connection을 종료하거나 X 이벤트 loop를 소유하지 않는다.
생성 thread에서 window-system 이벤트를 처리한 뒤 acquire/resize/present를 호출한다.
connection을 다른 thread와 공유하는 방식은 초기 지원 계약으로 넣지 않는다.

실제 XCB 사전 조사에서 RGBA8이 없는 조합이 발견됐다. Linux swapchain 구현에는
명시적인 `Format::BGRA8UNorm` 공개 형식 확장도 필요하다. `P3-WSI-03`에서 기존 enum 값을
유지해 형식 계약을 추가하고 native backbuffer·pipeline attachment·clear/draw·
transfer/footprint·readback byte 순서를 함께 구현·검증한다. BGRA8의 byte 순서는 B,G,R,A이며
RGBA8로 위장하지 않는다. 지원하지 않는 backend/usage는 명시적으로 거부하고 기존 format
분기의 fallthrough로 RGBA16F가 되지 않게 한다. 이번 scope 단계에서 형식 지원을 구현한 것은 아니다.

Core headless 경로에는 display 연결을 요구하지 않는다. XCB 구현의 compile/runtime 의존성은
선택 가능한 private 구성으로 추가하고 설치 헤더에 전파하지 않는다. SDK가 창 유효성/extent
확인에 필요한 XCB 함수를 사용할 경우 그 실제 compiler/link/dynamic 입력과 라이선스는
`P3-WSI-02`~`03`에서 감사한다. 이번 probe의 libxcb 링크는 SDK Core 의존성 추가가 아니다.

## Surface·queue·표현 형식 (`P3-WSI-02`~`03`)

필수 instance extension은 `VK_KHR_surface`, `VK_KHR_xcb_surface`,
`VK_KHR_get_surface_capabilities2`, `VK_EXT_surface_maintenance1`이고, device에는
`VK_KHR_swapchain`, `VK_EXT_swapchain_maintenance1`과 `swapchainMaintenance1=true`가 필요하다.
고정 Vulkan-Headers 1.3.290의 EXT 경로를 유지한다. 최신 KHR 이름만 있는 runtime 지원을
묵시적으로 주장하지 않으며 alias 지원 확대는 새 입력·동작 검증 뒤 결정한다.

현재 SDK와 같이 첫 graphics queue family를 선택하고 실제 window의
`vkGetPhysicalDeviceSurfaceSupportKHR`와 connection/visual의 XCB 표시 질의를 모두 확인한다.
지원하지 않으면 같은 adapter의 다른 family나 다른 GPU로 조용히 전환하지 않는다.
`caps.swapChain`은 구현과 device extension/feature의 사전 조건을 나타내며 특정 window의
유효성·실제 surface 지원은 생성 시 다시 확인해야 한다. 초기 device 생성에서 DISPLAY 연결을
요구하지 않는다. 물리 GPU ID와 software 종류는 기존 exact-selection 계약을 유지한다.

RGBA8 UNORM을 우선하고 없으면 BGRA8 UNORM을 명시 선택하며 색 공간은 SRGB nonlinear로 한다.
이는 새 BGRA8 공개 형식·pipeline/transfer 지원이 구현됐다는 전제의 **후속 설계**다.
실제 format/colorSpace 열거를 보존하고 SRGB native format을 UNORM으로 대체 해석하지 않는다.
color attachment + transfer-src/dst usage, 한 mip/layer/sample,
요청 buffer 2..4개와 실제 surface min/max를 검사한다. `vsync=true`는 FIFO,
false는 IMMEDIATE 우선·MAILBOX 차선이며 둘 다 없으면 Unsupported다. FIFO만으로 vsync=false를
성공 처리하지 않는다. extent는 현재 native client 크기와 surface min/max에 맞춰 검증한다.

## 완료·resize·손실 (`P3-WSI-04`~`06`)

초기 구현은 처리량 최적화보다 명시적 완료 보장을 우선하며 기존 Win32의 host-fenced 모델을
기준으로 삼는다. acquire fence로 획득 완료를 확인한 뒤 기록/제출하고, Present state와 resource
version을 검사한다. present wait semaphore는 native image별로 보관한다. present에는
maintenance1 completion fence를 연결하고 **presentation 자원 재사용·정리 가능 시점**까지 확인한다.
이 fence가 사람의 모니터 출력/scanout 완료를 의미한다고 주장하지 않는다.

[Khronos의 semaphore 수명 설명](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)에
따라 submit fence나 Queue/DeviceWaitIdle만으로 마지막 present의 semaphore·swapchain 해제 안전을
증명하지 않는다. maintenance1이 없는 legacy fallback은 초기 구현에서 채택하지 않는다.
timeout·present 오류 시 fence/semaphore 소유권과 안전한 정리 규칙을 별도로 대조한다.

0-size/minimized 시 caller는 렌더링을 멈추고 양의 실제 크기에서 resize한다. 살아 있는 backbuffer나
recording 참조가 있으면 resize를 거부한다. OUT_OF_DATE와 SURFACE_LOST는 nativeCode를 보존한
명시적 재구축 경로로 보고하고, 파괴된 native window는 새 caller-owned surface로 새 swapchain을
만든다. 실패한 native 재생성이 oldSwapchain을 퇴역시킨 경우에는 기존 buffer를 계속 쓰지 않으며
재시도 가능한 상태를 유지한다. SUBOPTIMAL·resize 경합·종료 직전 present의 수명도 검사한다.

DeviceLost는 `P3-CAP-02`처럼 이전 generation을 차단·퇴역시키고 동일 adapter로 새 generation을
반환한다. caller가 Linux swapchain과 CPU 기반 자원을 재구축해야 한다. 이 경로와 surface loss,
잘못된 window, 실제 TDR을 혼합하지 않는다.

## 후속 검증표 (`P3-WSI-07`~`08`)

| 계층 | 필요한 실제 근거 |
|---|---|
| Native 사전 조사 (이번 작업) | Linux XCB 창·VkSurface 생성, 실제 queue/visual/surface/format/usage/extension/feature, feature-enabled native VkDevice 생성, DISPLAY 부재 거부. SDK Linux WSI는 여전히 Unsupported |
| 공개 API/설치 | Windows 기존 초기화, Linux native handle 전달, platform header 비노출, 잘못된 종류/ID·thread·수명, headless 유지, 설치 소비자 컴파일 |
| GPU 참조 | 홀수 extent·전체 texel·padding, draw/copy/present, 재사용 image별 semaphore, render 생략 present, 마지막 present 직후 종료, backbuffer 참조 거부 |
| Resize/실패 제어 | live native window 크기 변경·0-size·복원, OUT_OF_DATE/SUBOPTIMAL/SURFACE_LOST, 실패 후 재시도, stale command/backbuffer, device loss 후 새 generation 재구축 |
| 필수 validation | 실제 Linux VVL의 core/sync/submit-time, 빈 settings 및 override 제거, 오류 검출 controls, 정상 ERROR 0, 실제 로드 파일·source/runtime 해시 |
| 물리 GPU/표시 | `P3-LNX-15`와 분리된 실제 Linux GPU/driver/선택·표시 경로. `P3-WSI-08`에서 사람이 보는 창·resize·가림/복원·닫기 관측. Xvfb 결과로 대체하지 않음 |

실제 사전 조사 결과와 source/CI 원본은 아래 실행 기록에 연결한다. `P3-WSI-01`의 완료는
범위·미지원 조합·검증 계획 결정이며 `P3-WSI-02`~`08`, 물리 GPU·Wayland 수용의 완료가 아니다.

## 실제 실행 기록

[Linux XCB 사전 조사 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40269532190)는
커밋 `f666c01a2b83268c5885f5996235479294753780`에서 성공했다. Ubuntu 24.04의 Xvfb
`21.1.12-1ubuntu1.9`, libxcb `1.15-1ubuntu2`, Vulkan loader `1.3.275.0-1build1`, Mesa
`25.2.8-0ubuntu0.24.04.4`를 사용했다. 실제 관측 장치는 llvmpipe `LLVM 20.1.2` software,
UUID `6d65736132352e322e382d3075627500`, native driverVersion `104865800`이다.

64×48 XCB 창과 VkSurface를 만들었고 첫 graphics family 0의 visual/surface 표시 질의가 모두
true였다. surface-capabilities2·surface-maintenance1·swapchain-maintenance1 extension과 feature가
지원됐으며 해당 device feature를 실제 켠 VkDevice 생성/파괴까지 성공했다. 표시 모드는
FIFO/IMMEDIATE/MAILBOX, min images 3/max 0(상한 없음), usage mask 524447이었다.
실제 format/colorSpace는 **50/0(BGRA8 SRGB), 44/0(BGRA8 UNORM)**뿐이었다. RGBA8 UNORM은
없었고, 그래서 후속 공개 BGRA8 형식·pipeline/transfer 구현을 필수 경로에 포함했다.
Vulkan format/colorSpace는 [실제 surface 질의](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceFormatsKHR.html)로 판단한다.

별도 process의 DISPLAY `:65533`은 XCB error 1, exit 2로 거부됐다. 양성 process는 exit 0이며
모든 관측 입력 해시가 실행 전후 같았다. Wayland extension은 열거됐지만 Wayland session은
실행하지 않았다. 설치 SDK의 Linux device는 여전히 `caps.swapChain=false`, 생성 Unsupported임을
같은 probe에서 확인했다. native/SDK swapchain 생성·present·draw/readback·VVL·사람의 출력 확인은
이번 scope probe에서 **실행하지 않았다**.

첫 [RGBA8 전용 전제의 조사](https://github.com/blackvrice/GraphicsSDK/actions/runs/40269180295)는
native query 자체 exit 0이었지만 RGBA8 부재로 policy-ready=0이라 runner가 exit 1로 거부했다.
이 원본과 당시 정확한 source ZIP을 보존했다. accepted source는 RGBA/BGRA의 실제 format row를
모두 기록하고 명시 BGRA 계약을 계획으로 검증한다. 초기 실패를 드라이버 crash로 분류하거나
성공 receipt로 바꾸지 않았다. 이 변화는 scope/probe에 한정하며 제품 형식 지원 구현은 아니다.

제품 SDK 27개 파일은 이전 `9614fd3`와 동일하다. 이번 실제 Linux SDK fingerprint는
`ca2f63fadcfdf373bde663c747e0bed626d071f737131d6e2d6daee2ff7c0f9e`이며 각 실행의 34개
source 파일 ZIP에 SDK와 정확한 probe·runner·workflow·계약 검사를 묶었다. 외부 headers는
`b379292b2ab6df5771ba9870d53cf8b2c9295daf`를 별도 고정하며 loader/ICD/libxcb binary는 재배포하지 않는다.
기존 GPU 검증을 이번 probe의 새 GPU/WSI 결과로 귀속하지 않는다.

probe 판정의 합성 대조 9개를 포함한 전체 Python **682개**가 로컬과 Windows/Linux CI에서 오류·실패·
건너뜀 0으로 통과했다. source/runtime/binary 식별 해시, package 버전, 원본 stdout/stderr와
[오프라인 검증기](../benchmarks/reports/validation/2026-10-26-phase3-linux-wsi-scope/verify_evidence.py),
[요약](../benchmarks/reports/2026-10-26-phase3-linux-wsi-scope/summary.json)을 보존한다.

재실행은 [전용 workflow](../.github/workflows/linux-wsi-scope.yml)를 따르거나 Linux에서 SDK를
새 경로에 build/install한 뒤 다음과 같이 수행한다. 출력 경로는 항상 새 경로를 쓴다.

```sh
cmake -S tests/linux_wsi_scope -B out/wsi-probe -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14 -DCMAKE_PREFIX_PATH="$PWD/out/wsi-install"
cmake --build out/wsi-probe --parallel 1
python3 tools/run_linux_wsi_scope.py --binary out/wsi-probe/gfx_linux_wsi_scope --sdk-library out/wsi-install/lib/libGraphicsSDKCore.a --icd /usr/share/vulkan/icd.d/lvp_icd.json --output out/wsi-scope-evidence-new
python3 benchmarks/reports/validation/2026-10-26-phase3-linux-wsi-scope/verify_evidence.py
```

이 CI 도구의 양성 집계 조건은 software 후보 전체가 위 사전 조건을 충족하는 것이다. native
query에서 미지원 조합을 기록할 수 있어도, 이를 자동으로 양성 수용으로 바꾸지는 않는다.
