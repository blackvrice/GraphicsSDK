# 배포 의존성 라이선스 감사

조사일: 2026-10-20. 범위는 Windows x64 Release SDK, LLGL/SDL3 기본 벤치마크와
추가 standalone Diligent Core 벤치마크 profile이다.
bgfx profile은 2026-10-21에 별도로 추가 검토했다.
다른 플랫폼·미빌드 경쟁 SDK·새 의존성에는 그대로 적용하지 않는다.

## 확인한 결과

SDK 패키지에서는 자체 MIT 코드만 확인했다. 실제 `GraphicsSDKCore.lib`, 공개 헤더,
CMake export, LICENSE를 ZIP 내용과 바이트 단위로 비교했고 경쟁 SDK DLL/정적 라이브러리는
없었다. Microsoft SDK/CRT/그래픽 드라이버는 시스템 요구사항이며 MIT로 재라이선스하지 않는다.
Microsoft 바이너리 재배포 조건은 [공식 배포 문서](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files)를
따라 별도로 확인해야 한다. 현재 ZIP에는 이 바이너리를 복사하지 않았다.

벤치마크의 compiler-read 프로젝트 파일은 965개, 분류 항목은 22개다. 이는 실제로
링크된 함수 965개나 번역 단위 965개를 뜻하지 않는다. 헤더와 조건부 컴파일에서 빠지는
소스, 정적 라이브러리의 미사용 부분까지 보수적으로 포함한다. 빌드 생성 파일 6개는
별도로 기록했다. 파일마다 SHA-256과 고정 Git 커밋의 원문 링크를 보존한다.

| 영역 | 확인한 조건 | 처리 |
|---|---|---|
| GraphicsSDK 코어·자체 어댑터 | MIT | 자체 고지 유지 |
| LLGL / DXCommon / D3D12 | BSD-3-Clause | 저작권·조건·면책·non-endorsement 고지 유지 |
| LLGL D3DX12 helper | Microsoft MIT | 정확히 일치하는 과거 원본과 라이선스 확보 |
| SDL 코어 및 SDL_gfx 파생부 | Zlib | 원본·변경 표시 및 각 저작권 고지 유지 |
| HIDAPI Windows 구현 | GPL / BSD / original 선택형 | BSD-3-Clause 대안을 명시적으로 선택 |
| SDL HIDAPI 공개 인터페이스 | Zlib + original HIDAPI 고지 | Alan Ott 원문 고지도 보존 |
| yuv2rgb | BSD-3-Clause | Adrien Descamps 고지 보존 |
| Sun fdlibm | SunPro | 소스의 permission/copyright 고지 보존 |
| Khronos Vulkan/OpenXR/video 헤더 | Apache-2.0 OR MIT | MIT 대안을 선택하고 저작권·permission 원문 보존 |
| EGL/GLES platform 및 EGL amalgamation | Apache-2.0, 일부 MIT/Zlib 혼합 | 파일 중간 고지도 추출하고 Apache 전문 보존 |
| Mesa/OpenGL, GameInput/DirectX 헤더 | MIT | 개별 저작권 및 MIT 고지 보존 |
| stb_image / miniz | MIT OR Unlicense / Unlicense | stb는 MIT 선택, miniz 원문 보존 |
| 폰트·stdlib·시간 계산 파생부 | Zlib 및 원문 public-domain/CC0 표기 | 저자와 원문의 고지 보존, 모두 동일한 단일 라이선스로 단정하지 않음 |

`SDL_JOYSTICK=OFF`만으로 HIDAPI 의존성이 없다고 판단하면 안 된다. 실제 compiler-read
기록에서 `src/hidapi/windows/hid.c`와 관련 헤더를 확인했다. Vulkan/OpenXR/GL 헤더가
읽혔다는 사실도 해당 런타임이나 GPU API를 이번 D3D12 측정에 사용했다는 뜻은 아니다.
GameInput도 조건부 코드일 수 있어 헤더/소스의 읽기와 DLL 사용을 구분한다.

## D3DX12 원본 확인

LLGL의 helper에는 Microsoft 저작권만 있고 MIT 전문이 없어 LLGL 루트 BSD를 적용하지 않았다.
Microsoft의 [고정 원본](https://github.com/microsoft/DirectX-Graphics-Samples/blob/44661685512abd973c82a8aa6244ec954a07bb80/Templates/DirectX12App/Common/d3dx12.h)을
LLGL 사본과 비교했다. CRLF→LF 및 파일 외곽 공백만 정규화하면 전체 내용이 일치한다.
정규화 SHA-256은 `6034badd9c6b3abb39985c6a4076c1b01651058d582ee4ba8fd3047fccadebd7`이다.
동일 커밋의 [MIT 라이선스](https://github.com/microsoft/DirectX-Graphics-Samples/blob/44661685512abd973c82a8aa6244ec954a07bb80/LICENSE)를
보존했고 감사 도구는 이 본문 digest가 달라지면 실패한다. 단순히 최신 동명 헤더의
라이선스를 오래된 사본에 추정 적용한 결과가 아니다.

## 자동 검증과 산출물

`tools/audit_licenses.py`는 네트워크 없이 다음을 검증한다.

1. 고정 커밋과 검토 정책, 실제 checkout의 tracked 변경 여부.
2. MSBuild Release compiler-read 및 실제 linker-read 입력과 파일별 해시. 미검토 `.lib`는 외부 헤더가 없어도 거부한다.
3. D3DX12 출처 digest, 실제 코어 라이브러리의 source fingerprint, LLGL runtime copy 일치.
4. 파일 처음뿐 아니라 amalgamated header 중간과 stb 헤더 끝의 고지.
5. SDK ZIP의 필수 파일·라이선스 일치 및 미검토 payload/경로/Windows 이름 충돌 거부.
6. 생성한 inventory와 notice의 변조·누락·빌드 변경을 `--check`로 감지.

실제 산출물과 실행 명령은 [licenses/README](../licenses/README.md)에 있다.
원본 benchmark 측정 JSON/CSV, source snapshots, 기존 개발 ZIP은 변경하지 않았다.
기본 감사 산출물은 당시 구성의 기록이다. 이후 어댑터 변경에는 아래의 별도 profile을 사용한다.

## Diligent Core 추가 profile

`benchmarks-diligent`는 새 빌드의 compiler/linker 기록을 사용한다. compiler-read 파일과
생성 shader의 명시적 원본 10개를 합쳐 프로젝트 입력 1,349개, 고지 분류 26개,
생성 파일 15개를 기록했다. Core 루트 Apache-2.0 외에
vendored DXC 헤더 NCSA, AMD DXBC checksum의 MIT 및 RSA-MD5 파생 조건,
실제로 링크된 `nvapi64.lib`의 명시적 MIT 조건을 구분했다. 이 checksum은
RSA Data Security, Inc. MD5 Message Digest Algorithm에서 파생된 코드이며 해당
attribution/파생물 표시 조건과 저작권 원문을 고지에 유지한다.

Core/xxHash/Abseil/NVAPI 고정 버전을 모두 검사한다. xxHash와 Abseil은 구성 과정에서
필요하지만 이번 D3D12 target의 compiler-read 입력에서는 발견되지 않아 컴파일된
구성요소로 세지 않는다. 반면 NVAPI는 헤더와 x64 library의 실제 linker 입력을 모두
확인했다. 자체 SDK ZIP에는 이들 라이브러리나 Diligent DLL을 추가하지 않았다.

추가 [검토 정책](../licenses/diligent-policy.json)과
[inventory](../licenses/generated/windows-benchmarks-diligent-release/inventory.json),
[고지 원문](../licenses/generated/windows-benchmarks-diligent-release/THIRD_PARTY_NOTICES.txt)을
보존했다. 생성 mip shader와 원본 HLSL도 기록한다. native debug layer/PIX/driver 및
DXC runtime 사용·배포 권한을 compiler-read 헤더만으로 추정하지 않는다.
새 profile은 Diligent DLL 복사 일치, 실행 파일의 소스/버전 fingerprint와 실제 compiler
command의 `/O2 /MD`, LTO 미사용을 추가로 검사한다.

## bgfx 추가 profile

`benchmarks-bgfx`는 기본 LLGL/SDL3와 새 bgfx/bx/bimg static libraries, 어댑터, 코어의
실제 MSBuild compiler/linker 기록을 검사한다. 프로젝트 입력 1,155개, 분류 30개,
생성 SDL 파일 6개를 기록했다. bgfx/bx/bimg 루트는 BSD-2-Clause이며 RenderDoc API
헤더 MIT, bx의 dirent MIT, Milo Yip dtoa의 MIT, Matthew Endsley TinySTL BSD와
Ozan Yigit sdbm public-domain attribution을 별도로 유지했다.

dirent 헤더 자체는 Toni Ronkko와 MIT를 명시한다. 이를 보완할 공식 upstream LICENSE
전문을 [고정 출처](../licenses/provenance/dirent-license-source.json)와 함께 보존했다.
D3DX12처럼 전체 소스 본문이 일치하는 원본을 찾았다는 주장은 아니다. bgfx의
amalgamated 파일 목록에는 Metal/Vulkan 등의 조건부 파일도 읽히지만 이 빌드에서
그 backend를 실행하거나 제공했다는 뜻은 아니다. ASTC codec 및 image-decode/encode
도구, shaderc 도구는 빌드·배포 대상이 아니다.

[추가 정책](../licenses/bgfx-policy.json),
[inventory](../licenses/generated/windows-benchmarks-bgfx-final-release/inventory.json),
[고지 원문](../licenses/generated/windows-benchmarks-bgfx-final-release/THIRD_PARTY_NOTICES.txt)을
보존했다. static bgfx library와 실행 파일의 compiled fingerprint 및 실제 `/O2 /MD`,
LTO 미사용·D3D12·single-thread·video OFF 옵션을 검사한다. 드라이버의 NVAPI와
RenderDoc/Aftermath 등 선택형 runtime이 배포물에 포함됐다고 추정하지 않는다.
기존 자체 SDK ZIP에는 경쟁 코드나 DLL을 추가하지 않았다.

고지 모음은 배포 시 사용할 자료이며 `inventory_verified`는 법적 배포 승인 표시가 아니다.
실제 배포 구성, SDK/백엔드/옵션/버전, 추가 라이브러리 또는 시스템 DLL 포함 범위가 달라지면
다시 검토한다. Python plotting 패키지는 도구 전용이며 현재 배포 대상에서 제외했다.
모든 미래 경쟁 SDK의 전이 의존성을 검토 완료했다고 주장하지 않는다.

초기 `windows-benchmarks-bgfx-release`는 copy 기반 탐색 빌드의 기록으로 보존한다.
최종 makeRef 빌드는 `windows-benchmarks-bgfx-final-release`의 실제 artifact hash를 사용한다.

## The Forge 추가 profile

`benchmarks-forge`는 입력 1,102개·분류 39개 및 생성 SDL/config 파일을 기록한다.
원본 C D3D12 backend, D3D12MA, single-thread resource loader, Windows utility와 필요한
LZ4/Zstd 소스를 빌드한다. Apache 루트와 AGS/D3D12MA/Agility header MIT, DXC NCSA/MIT,
RenderDoc API MIT, Sony BSD/Forge Apache, bstrlib BSD, LZ4 BSD, Zstd BSD 선택, STB MIT,
tiny DDS/KTX/ImageFormat MIT, MurmurHash public-domain 고지를 구분했다.

동봉 legacy NVAPI의 2016 계약은 competitive benchmark 결과 공개를 제한한다. 최종
프로파일은 NVAPI helper의 기존 fallback을 선택하고 이 proprietary header/library를
컴파일·링크 입력에서 제외한다. 실제 compiler-read와 linker-read 기록으로 강제하며
해당 라이브러리를 사용한 탐색 결과를 공개 성능 campaign에 포함하지 않는다.
NVIDIA 전용 확장이나 공식 VS 2019 sample 전체를 측정한 프로파일이 아니다.

DXC `dxcapi.h`, `dxcompiler.lib`, `dxcompiler.dll`은 공식 v1.8.2405 Windows 배포 ZIP과
byte-identical이다. [출처·해시 증거](../licenses/provenance/forge-terms-origin.json) 및 공식
LLVM/NCSA·Microsoft MIT 고지문을 보완했다. `dxil.dll`과 MS binary terms는 참고 고지로만
보존하고 DLL은 복사하지 않는다. TinyImageFormat의 명시적 MIT 표기는 공식 고정 커밋의
전체 MIT 원문으로 보완했으며 source-body 전체 일치로 주장하지 않는다.

PIX 1.0.200127001 header 두 개는 compiler-read에 포함되지만 `USE_PIX`를 끄고
PIX library/DLL은 링크·복사하지 않는다. 동봉 NuGet metadata의 historical licenseUrl은
현재 다른 문서로 이동하고 primary terms가 없어 header 재배포 조건은 미해결로 남긴다.
이를 Apache/MIT로 재분류하지 않으며 `LicenseRef-Microsoft-PIX-Event-Runtime`과 한계를
명시한다. 현재 결과는 로컬 빌드 입력 inventory이며 경쟁 바이너리/헤더의 배포 승인이 아니다.

[정책](../licenses/forge-policy.json),
[inventory](../licenses/generated/windows-benchmarks-forge-release/inventory.json),
[고지](../licenses/generated/windows-benchmarks-forge-release/THIRD_PARTY_NOTICES.txt)에 기록했다.
실제 `/O2 /MD`·no LTO, 생성 외부 config 내용, compiled source fingerprint, 원본·복사본
AGS/DXC DLL 및 GPU config 파일을 검사한다. 자체 SDK ZIP에는 경쟁 payload를 추가하지
않았고 기존 SDK package `--check`가 통과했다.

## Timestamp 확장 inventory

query 어댑터 확장 후 새 산출물 해시를 별도로 보존했다. 기존 inventory를 덮어쓰지 않는다.
[Forge](../licenses/generated/windows-benchmarks-timestamps-forge-release/inventory.json)는
입력 1,103개/구성요소 39개이고
[Diligent](../licenses/generated/windows-benchmarks-timestamps-diligent-release/inventory.json)는
입력 1,350개/구성요소 26개다. 각각 옆의 `THIRD_PARTY_NOTICES.txt`를 유지한다.
LLGL 공개 native-handle 헤더로 validation queue를 조회하며 외부 query 코드나 DLL을
SDK에 복사하지 않는다. 동일 pinned 정책·실제 compiler/linker 입력·소스 fingerprint 및
runtime copy를 검사했다. 자체 SDK ZIP은 기존 `--check`가 통과했다.
Forge의 비활성 PIX header primary terms 미해결과 경쟁 바이너리 재배포 승인 미포함은
그대로이며 GPU query 추가가 이를 해결했다는 의미가 아니다.

## CPU/memory 공통 profile inventory

선택형 OS 관측을 추가한 공통 LLGL/SDL3/GraphicsSDK 실행 파일은 새
[inventory](../licenses/generated/windows-benchmarks-telemetry-release/inventory.json)와
[고지](../licenses/generated/windows-benchmarks-telemetry-release/THIRD_PARTY_NOTICES.txt)에
968개 compiler-read 프로젝트 입력/22개 구성요소를 기록했다. Windows PSAPI/DXGI는
시스템 전제조건이며 새 competitor나 runtime 패키지를 추가하지 않는다. 기존 inventory와
SDK ZIP을 덮어쓰지 않았다. optional Forge/Diligent/bgfx profile의 새 계측 artifact 감사나
다른 플랫폼 검증을 수행했다는 의미가 아니다. 새 공통 inventory 및 기존 SDK ZIP의
`--check`가 통과했다.

Phase 1 advanced baseline은 별도 `benchmarks-phase1` 감사 profile을 사용한다.
공통 `gfx_bench`와 새 `phase1_llgl` target의 compiler/linker 입력을 함께 수집하며
[최종 inventory](../licenses/generated/windows-benchmarks-phase1-final-release/inventory.json)와
인접 고지문에 970개 프로젝트 입력/22개 구성요소를 기록한다. 새 third-party package나
vendor patch는 없고 같은 fixed LLGL/SDL 입력 및 Windows/D3DCompiler/PSAPI 시스템
전제조건을 유지한다. 초기 탐색 inventory는 별도로 보존한다. 자체 SDK/API/ZIP은
변경하지 않았으며 inventory와 기존 SDK package 검사가 통과했다.

LLGL 공개 upload 복구 후에는 별도
[inventory](../licenses/generated/windows-benchmarks-llgl-upload-release/inventory.json)와
같은 폴더의 고지문을 생성했다. 공통 profile의 compiler-read 입력 969개/구성요소 22개,
새 adapter/harness 및 binary 해시를 기록한다. competitor pin과 원본 checkout은 그대로이며
새 외부 라이브러리는 없다. 새 inventory와 변경하지 않은 SDK ZIP의 `--check`가 통과했다.
이전 inventory/측정 소스 ZIP을 덮어쓰지 않는다.
