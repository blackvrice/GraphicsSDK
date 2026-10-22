# Phase 2 백버퍼의 현재 대상·스왑체인 수명 검증

2026-10-22. 현재 대상이 아닌 백버퍼와 파괴된 swapchain의 백버퍼를 기록/제출하는
누락을 수정했다. 성공한 present는 기록 version도 변경한다. **이 작업은 공개 수명 계약의
SDK 수정이며, 육안 표시·실제 DPI·예기치 않은 TDR/hot-unplug나 장시간 GBV 메모리 gate의
완료가 아니다.** 새 SDK fingerprint는
`165288f2cd9e3f820d5e08333ea0d129533a5fcab201490dee41b1df1c8be8fa`다.

## 수정 전후 재현

기존 설치 SDK fingerprint `07260dcfd152001342b801d0206bab594109b5d9046e58eff5694b4ba98a1fa6`의
WARP 최소 소비자에서 정상 clear/submit/present/wait 뒤 이전 TexturePtr로 transition을
기록할 수 있었다. swapchain 파괴 뒤 보유한 다른 백버퍼에도 transition 기록이 허용됐다.
두 `*_recording_accepted` 값은 모두 1, 실행 exit는 2였다. 잘못된 명령은 GPU에 제출하지
않았다. 같은 소비자를 새 설치 SDK에 재링크하면 두 값이 0이고 exit 0이다.

[Microsoft의 D3D12 swapchain 계약](https://learn.microsoft.com/en-us/windows/win32/direct3d12/swap-chains)은
현재 backbuffer index 조회와 Present 상태 요구를 구분한다. 기존 SDK는 resource state만
검사하고 해당 resource가 현재 buffer인지, 공개 swapchain이 살아 있는지 확인하지 않았다.
이번 수정은 native 오류를 의도적으로 실행해서 감지하는 방식 대신 SDK의 입력·제출 경계에서
거부한다. native DXGI/driver 실패나 장치 제거를 이 재현의 결과로 주장하지 않는다.

## 변경된 계약

각 백버퍼 tracker에 swapchain별 약한 scope와 buffer index를 저장한다. scope는 native
chain의 non-owning pointer를 제공하며 명시적인 texture 입력과 submit에서 실제 current
index를 확인한다. 공개 swapchain 파괴/퇴역은 scope를 무효화한다. TexturePtr/명령이
public wrapper를 강하게 보유하는 순환 참조는 추가하지 않는다. 생성 thread 계약을 유지하며
동시 파괴/기록을 지원하는 새 thread-safety 계약은 아니다.

건강한 같은 device에서 noncurrent/파괴된 chain의 texture transition, render attachment,
copy 및 기록된 명령의 submit은 InvalidState다. submit 검사는 native 실행·fence signal·
resource state commit 전에 끝난다. 기존 완료 제출의 정리는 기존대로 수행할 수 있다.
다른 device/generation은 WrongDevice, 관측된 손실/퇴역은 DeviceLost가 먼저 적용된다.
description/footprint 같은 CPU metadata는 파괴 뒤에도 읽을 수 있다.

성공한 present는 제시한 buffer tracker version을 증가시킨다. 따라서 present 전에 기록한
미제출 명령은 한 바퀴 회전해 같은 TexturePtr가 current가 되어도 거부된다. 같은 pointer로
새로 기록한 명령은 current인 동안 정상 사용한다. version 소진은 native Present 전에 거부한다.
다른 swapchain의 present/파괴는 독립적이다. 정상 submit → present와 참조 해제 뒤 resize
규칙, resource 통계와 손실 복구 계약은 유지한다. 일반 texture/buffer에는 native current-index
조회를 추가하지 않는다. 공개 virtual ABI나 native 접근 API는 추가하지 않았다.

## 실제 검증

NVIDIA GTX 1660 SUPER / Intel UHD 770 / WARP의 정확한 ID를 새로 열거해 선택하고, 일반과
전체 GBV 두 모드에서 실행했다. 총 **18개 2/3/4-buffer ring case, 90개 전체 이미지,
42,270 RGBA pixels, 258개 거부 검사, 6개 controlled recovery**를 검증했다.

각 ring case는 63×7의 row-padded clear/readback 세 이미지와 resize 후 65×9 한 이미지를
확인한다. noncurrent transition 네 상태, color attachment, current/noncurrent 혼합 MRT,
upload/readback 거부 뒤 같은 command list로 정상 전체 이미지를 검증해 기록의 원자성을
확인한다. 잘못된 submit은 fence timeline을 바꾸지 않아야 한다. 순환만을 위한 present와
참조 검증용 clear/readback frame은 구분하며, 읽지 않은 내용을 이미지 합격으로 세지 않는다.

현재 대상에서 기록한 명령을 present 직후와 전체 ring 순환 후에 각각 거부하고, retained
TexturePtr의 새 기록은 성공한다. 보유 참조의 resize 거부와 해제 뒤 resize 성공, chain 파괴
뒤 metadata 보존·새 기록/기존 기록 제출 거부를 확인한다. 두 별도 HWND의 53×11 swapchain은
한쪽의 present/파괴 뒤 다른 쪽 기록을 제출하고 두 전체 이미지를 검증한다.

native RemoveDevice/recover 뒤 같은 HWND의 새 31×5 chain에서 전체 이미지를 검증한다.
이전 chain은 원래 native reason의 DeviceLost, 새 generation에서 이전 texture는 WrongDevice다.
acquire의 thread 거부와 건강한 원래/복구 generation의 validation error 0도 확인한다.
이 자동화는 실제 디스플레이 scanout이나 예기치 않은 물리 제거의 검증이 아니다.

Core CTest **32/32**, Windows D3D12 OFF **6/6**, 공통 correctness **72/72**, 창 예제
**7/7**, Python **159/159**와 기존 설치 소비자가 통과했다. 기존 창/명시 adapter campaign의
441개 전체 이미지·67,393,539 pixels도 새 SDK에서 다시 검증했다. 장시간 캠페인은 이번에
다시 수행하지 않았으며 원래의 실패와 policy를 보존했다. 성능 우위도 주장하지 않는다.

## 배포와 근거

새 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-swap-guards.zip`이며 기존 9개 ZIP은
그대로 보존한다. SDK 수정에 맞춰 관련 library·benchmark·설치 소비자·진단/심볼 도구를
재빌드했다. 새 [SDK 20/1](../licenses/generated/windows-sdk-swap-guards-release/inventory.json),
[공통 978/22](../licenses/generated/windows-benchmarks-swap-guards-release/inventory.json),
[swap/설치/창 42/1](../licenses/generated/windows-sdk-swap-guards-validation-release/inventory.json),
[stress 26/1](../licenses/generated/windows-sdk-swap-guards-stress-release/inventory.json),
[crash 28/1](../licenses/generated/windows-sdk-swap-guards-crash-release/inventory.json) 감사를 수행한다.
`sdk-swaps`는 기존 waits profile에 자체 `tests/SwapChainTests.cpp`만 추가 허용하고 총 8개
executable linker closure를 확인한다. 새 외부 dependency/runtime 배포는 없다.
자체 source ZIP에는 실행 파일·library·PDB·dump를 넣지 않는다.

[직렬 검증 원본](../benchmarks/reports/validation/2026-10-22-phase2-swap-guards/)과
[요약·해시·source archive](../benchmarks/reports/2026-10-22-phase2-swap-guards/)를 보존한다.
원래 장시간 GBV gate와 Intel 크래시 원인, 육안 표시/실제 DPI/예기치 않은 TDR·hot-unplug 및
Vulkan/Linux/Metal/macOS는 여전히 남아 있다.
