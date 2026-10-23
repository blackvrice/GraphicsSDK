# 배포 의존성 라이선스 감사

GPU 타임스탬프는 `audit_vulkan.py --variant timestamps`와
`licenses/vulkan-timestamps-policy.json`으로 별도 감사한다.
[inventory](../licenses/generated/windows-sdk-vulkan-timestamps-release/inventory.json)는
80개 compiler input/2개 component, 34개 compiler log/33개 linker log와 정확한 SDK payload를
검사한다. 자체 private 변환 helper·CPU/GPU 테스트 세 파일만 허용 범위에 추가했다.
기존 depth shader fixture·고정 local tool/runtime은 SHA로 확인해 재사용하며 이번에 새 shader를
조립/검증했다고 주장하지 않는다. 외부 dependency 배포를 추가하지 않고 기존 SDK ZIP 20개를 보존한다.

D32 깊이 패스는 `audit_vulkan.py --variant depth`와 `licenses/vulkan-depth-policy.json`으로
별도 감사한다. [inventory](../licenses/generated/windows-sdk-vulkan-depth-release/inventory.json)는
77개 compiler input/2개 component, 31개 compiler log/30개 linker log와 정확한 SDK payload를
검사한다. 자체 GPU 테스트·지원/fixture header 세 파일만 허용 범위에 추가했다.
기존 고정 로컬 shader tool/runtime을 재사용하고 외부 dependency 배포를 추가하지 않는다.
기존 SDK ZIP 19개와 과거 inventory를 보존한다.

리소스 바인딩은 `audit_vulkan.py --variant bindings`와
`licenses/vulkan-bindings-policy.json`으로 별도 감사한다.
[inventory](../licenses/generated/windows-sdk-vulkan-bindings-release/inventory.json)는
74개 compiler input/2개 component, 29개 compiler log/28개 linker log와 정확한 SDK payload를
검사한다. 새 자체 CPU/GPU 테스트·지원/fixture header 네 파일만 허용 범위에 추가했다.
기존 고정 로컬 shader tool/runtime만 재사용하고 외부 dependency 배포는 추가하지 않는다.
기존 SDK ZIP 18개와 과거 inventory는 보존한다.

정점·인덱스 입력 SDK는 `audit_vulkan.py --variant vertex`와
`licenses/vulkan-vertex-policy.json`으로 별도 감사한다.
[inventory](../licenses/generated/windows-sdk-vulkan-vertex-release/inventory.json)는
70개 compiler input/2개 component, 26개 compiler log/25개 linker log와 정확한 설치 SDK
payload를 확인한다. 새 자체 테스트·fixture header 네 파일만 허용 범위에 추가했다.
기존 고정 assembler/validator와 VVL runtime은 로컬 도구로 재사용하고 hash를 확인한다.
외부 vendor source/binary는 SDK/source ZIP에 넣지 않으며 기존 17개 SDK ZIP을 보존한다.

SPIR-V draw SDK는 `audit_vulkan.py --variant draw`와 `licenses/vulkan-draw-policy.json`으로
독립 감사한다. [inventory](../licenses/generated/windows-sdk-vulkan-draw-release/inventory.json)는
실제 SDK/CPU·GPU 테스트/설치 소비자 입력과 SDK payload를 확인한다. 자체 shader assembly와
생성 word 배열은 GraphicsSDK 소스다. 로컬 전용 fixture assembler/validator만 기존 고정
SPIRV-Tools 라이브러리를 사용한다. 해당 DLL/LIB/EXE/vendor header·source는 SDK/source ZIP에
넣지 않으며, 로컬 도구 receipt의 SHA·라이선스 해시는 SDK 배포 감사와 구분한다.

색상 렌더 패스는 `audit_vulkan.py --variant color-pass`와
`licenses/vulkan-color-pass-policy.json`으로 별도 감사한다. Native render pass,
framebuffer/image view는 기존 고정 Vulkan-Headers 선언만 사용하고 새 외부 코드를
추가하지 않는다. [inventory](../licenses/generated/windows-sdk-vulkan-color-pass-release/inventory.json)는
통합 SDK/테스트와 설치된 buffer·texture·color-pass 소비자의 compiler/linker 입력 및
정확한 SDK payload를 확인한다. 이전 모든 inventory와 로컬 패키지를 보존한다.

색상 텍스처 확장은 `audit_vulkan.py --variant textures`와
`licenses/vulkan-textures-policy.json`으로 별도 감사한다. 기존 두 Vulkan
inventory와 로컬 SDK ZIP을 보존하며 외부 런타임은 추가하지 않는다.
새 [inventory](../licenses/generated/windows-sdk-vulkan-textures-release/inventory.json)는
통합 SDK/테스트와 설치된 buffer·texture 소비자의 실제 compiler/linker 입력 및
SDK payload를 확인한다. 고정 Vulkan-Headers Apache-2.0/MIT 고지 범위는 동일하다.

Vulkan synchronization-validation 구성은 `audit_vulkan.py --variant validation`과
`licenses/vulkan-validation-policy.json`으로 별도 감사한다. 공용 SDK에 추가 외부 static
library를 링크하지 않으며 새 private validation 설정/콜백과 native probe는 자체 MIT 입력이다.
레이어/Vulkan Utility/SPIRV/robin-hood는 정확한 SHA로 고정한 **로컬 검증 도구 의존성**이며
새 SDK/source ZIP에 vendor source, DLL, PDB, LIB를 묶지 않는다. 로컬 빌드의 명령·옵션·원본
라이선스 해시와 runtime SHA를 별도 검증 근거에 기록한다. 이 기록은 해당 외부 runtime의
재배포 허가 또는 전체 링크 도달성 감사가 아니다. SDK payload의 Vulkan-Headers 11개 입력과
Apache/MIT 고지는 [새 inventory](../licenses/generated/windows-sdk-vulkan-validation-release/inventory.json)에서 검증한다.

2026-10-23 Vulkan 전송 variant는 기존 D3D12 inventory와 별개로
[`audit_vulkan.py`](../tools/audit_vulkan.py)를 사용한다. 고정된 Vulkan-Headers
`v1.3.290`/`b379292b2ab6df5771ba9870d53cf8b2c9295daf`의 실제 compiler-read
11개 헤더는 Apache-2.0 전용 SPDX이며, MIT 대안을 선택하지 않는다.
정책·헤더 해시, upstream Apache 원문과 각 파일의 저작권/SPDX 고지를 보존한다.
SDK/테스트/예제와 설치 소비자의 실제 Release compiler/linker closure 및 설치본 일치를
검사하고, 새 ZIP의 정확한 payload와 고지를 검증한다. Loader/ICD/driver/validation layer는
배포하지 않는다. [새 inventory](../licenses/generated/windows-sdk-vulkan-transfer-release/inventory.json)는
Windows x64 Release의 이 구성에만 적용하며 과거 binary/benchmark inventory를 대체하지 않는다.

```powershell
python tools/audit_vulkan.py --output licenses/generated/windows-sdk-vulkan-transfer-release --package out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-transfer.zip --create
python tools/audit_vulkan.py --output licenses/generated/windows-sdk-vulkan-transfer-release --package out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-transfer.zip --check
```

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

SDL3 E–H 추가 비교는 같은 profile에 `phase1_sdl`의 compiler/linker 입력도 포함했다.
[추가 비교 inventory](../licenses/generated/windows-benchmarks-phase1-sdl-release/inventory.json)와
인접 고지문에 972개 프로젝트 입력/22개 구성요소를 보존한다. 고정 dependency와 기존 SDK
ZIP은 변경하지 않았으며 별도 폴더에 새 artifact 해시를 기록한다. 이전 감사 결과는 보존한다.

GPU 메모리 acceptance의 별도 `benchmarks-gpu-memory` profile은 위 입력에
`gpu_etl_export`/`gpu_memory_control`의 실제 compiler/linker 입력을 추가한다.
[새 inventory](../licenses/generated/windows-benchmarks-gpu-memory-release/inventory.json)는
974개 프로젝트 입력/22개 구성요소를 기록한다. 두 신규 자체 C++ 파일만 `tools/gpu_trace/`
아래에서 명시적으로 허용하며 임의 tools 파일을 MIT 입력으로 자동 분류하지 않는다.
TDH/ETW/DXGI는 Windows 시스템 전제조건이고 새 third-party package는 없다.
자체 SDK/API/ZIP은 변경하지 않았으며 새 inventory와 기존 SDK ZIP의 `--check`가 통과했다.

LLGL 공개 upload 복구 후에는 별도
[inventory](../licenses/generated/windows-benchmarks-llgl-upload-release/inventory.json)와
같은 폴더의 고지문을 생성했다. 공통 profile의 compiler-read 입력 969개/구성요소 22개,
새 adapter/harness 및 binary 해시를 기록한다. competitor pin과 원본 checkout은 그대로이며
새 외부 라이브러리는 없다. 새 inventory와 변경하지 않은 SDK ZIP의 `--check`가 통과했다.
이전 inventory/측정 소스 ZIP을 덮어쓰지 않는다.

Phase 2 graphics binding 확장은 별도
[SDK inventory](../licenses/generated/windows-sdk-bindings-release/inventory.json)와
[공통 benchmark inventory](../licenses/generated/windows-benchmarks-bindings-release/inventory.json)를
생성했다. 각각 17개 입력/자체 구성요소 1개, 975개 입력/구성요소 22개이며 인접 notices를 보존한다.
새 SDK ZIP과 두 `--check`가 통과했다. 기존 SDK/vertex-input ZIP과 과거 inventory는 그대로다.
신규 외부 의존성과 competitor 수정은 없으며 위 compiler-read/package 감사 범위를 유지한다.

Depth/MRT 확장은 별도 [SDK inventory](../licenses/generated/windows-sdk-depth-mrt-release/inventory.json)와
[공통 benchmark inventory](../licenses/generated/windows-benchmarks-depth-mrt-release/inventory.json)를
추가했다. 각각 compiler-read 입력 19개/자체 구성요소 1개와 입력 977개/구성요소 22개다.
새 SDK ZIP과 두 `--check`가 통과했고 인접 고지문을 보존한다. 새 외부 의존성과 competitor 수정은
없으며 최초 SDK/vertex-input/bindings ZIP과 모든 과거 감사·측정·소스 archive를 보존한다.

장치 손실 복구의 별도 [SDK](../licenses/generated/windows-sdk-recovery-release/inventory.json)와
[공통 benchmark](../licenses/generated/windows-benchmarks-recovery-release/inventory.json) inventory는
입력 19개/자체 구성요소 1개 및 입력 977개/구성요소 22개를 기록한다. 새 복구 SDK ZIP과
두 `--check`가 통과했고 인접 고지문을 보존한다. Native D3D12 RemoveDevice는 기존 Windows
시스템 전제조건이며 새 외부 package나 competitor 수정은 없다. 이전 네 SDK ZIP·감사·측정·archive는 그대로다.

메모리 진단과 오류 callback 수정에는 별도
[SDK](../licenses/generated/windows-sdk-memory-stress-release/inventory.json),
[스트레스/진단 도구](../licenses/generated/windows-sdk-stress-release/inventory.json),
[공통 benchmark](../licenses/generated/windows-benchmarks-memory-stress-release/inventory.json) inventory와
인접 notices를 생성했다. 각각 compiler-read 입력/구성요소는 19/1, 21/1, 977/22다.
`sdk-stress`는 SDK 외에 `tools/MemoryStress.cpp`, `tools/memory_validation_probe/main.cpp`만
명시적으로 허용하며 두 exe의 실제 linker 입력도 검사한다. 새 SDK ZIP과 세 `--check`가 통과했다.
PSAPI/DXGI/D3D12 debug callback은 기존 Windows 시스템 전제조건이다. 외부 dependency나
competitor 수정은 없고 이전 다섯 SDK ZIP 및 모든 과거 측정·감사·source archive를 보존했다.
감사 통과는 메모리 acceptance 통과를 뜻하지 않는다. GBV 증가로 장시간 gate는 실패 상태다.

GBV 자원 재사용과 device lifetime 분리는 각각 이전
[22/1 inventory](../licenses/generated/windows-sdk-gbv-reclamation-release/inventory.json)와 새
[24/1 inventory](../licenses/generated/windows-sdk-device-lifetime-final-release/inventory.json)에 보존한다.
현재 `sdk-stress`는 기존 두 C++ 도구, `ReusableFrame.hpp`, `Lifetime.cpp`, `SDKLifetime.cpp`만
명시적으로 추가 허용한다. 네 exe의 실제 linker 입력을 감사하며 native-only `gbv_lifetime_probe`는
SDK library를 링크하지 않는다. 기존 SDK/common 감사와 SDK ZIP 내용 검사는 그대로 통과했다.
네 Windows runtime DLL은 경로·해시만 기록하며 재배포하지 않는다. 새 외부 dependency는 없다.

추가 runtime 비교는 [최종 25/1 inventory](../licenses/generated/windows-sdk-runtime-comparison-final-release/inventory.json)에
`Runtime.cpp`와 다섯 번째 도구의 실제 linker 입력을 명시적으로 포함한다. 별도 provenance는
공식 Microsoft.Direct3D.D3D12 1.619.6 package/DLL/license 해시와 Microsoft 서명을 기록한다.
binary는 Microsoft DirectX 약관이며 MIT header와 구별한다. package header는 컴파일하지 않고
binary/package는 local Windows 진단 `out/`에만 둔다. SDK ZIP/source archive에는 넣지 않는다.
컴파일 inventory의 자체 MIT 한 구성요소가 실행용 Microsoft binary까지 포괄한다는 뜻은 아니다.

Win32 창 표시 예제는 별도 `sdk-presentation` profile과
[33/1 inventory](../licenses/generated/windows-sdk-presentation-release/inventory.json)를 추가한다.
자체 `Windowed.cpp`만 명시적으로 허용하고 compiler-read에 나타난 설치 public header를 현재
원본과 byte 단위로 대조한다. 설치 static library도 현재 core와 같아야 한다. 실제 linker-read는
설치 SDK와 검토된 Windows/CRT library만 허용하며 sample CMake/manifest와 notices를 보존한다.
새 SDK package나 외부 dependency는 없고 기존 SDK ZIP·SDK/common/stress `--check`도 통과했다.
이는 자동 창·이미지 검증의 빌드 입력 감사이며 육안 표시/실제 DPI 변경/예상치 못한 장치 손실
acceptance를 부여하지 않는다.

작은 창·프레임 속도 조절 보강은 별도
[최종 35/1 inventory](../licenses/generated/windows-sdk-presentation-pacing-final-release/inventory.json)를
추가한다. `PresentationPolicy.hpp`와 `PolicyTests.cpp`만 추가 입력으로 허용하고 CPU 정책
실행 파일의 실제 system-only linker 입력도 검사한다. 고해상도 waitable timer는 기존
Windows Kernel32 전제조건이며 DLL/package를 배포하지 않고 전역 timer 설정도 바꾸지 않는다.
초기 coarse-timer inventory와 기존 33/1 inventory는 보존하며, 최종 소스·실행은 새 inventory에
대응한다. SDK/common/stress·SDK ZIP 감사와 6개 기존 SDK ZIP 해시가 유지된다.

공개 adapter API에는 새 [SDK 19/1](../licenses/generated/windows-sdk-adapters-release/inventory.json),
[공통 benchmark 977/22](../licenses/generated/windows-benchmarks-adapters-release/inventory.json),
[선택/설치/창 검증 37/1](../licenses/generated/windows-sdk-adapters-validation-release/inventory.json),
[진단 도구 25/1](../licenses/generated/windows-sdk-adapters-stress-release/inventory.json) inventory를 생성했다.
`sdk-adapters`는 기존 창 profile에 `tests/AdapterTests.cpp`, `tests/consumer/adapter.cpp`만
명시적으로 추가한다. 네 실행 파일의 actual linker closure와 설치 header/library 일치를 검사한다.
SDK가 변경되어 library·benchmark·consumer·진단 도구를 재빌드하고 새 adapters SDK ZIP을 감사했다.
DXGI/D3D12는 기존 Windows 전제조건이며 외부 dependency나 runtime 배포는 추가하지 않았다.
6개 과거 SDK ZIP·과거 inventory·원본 근거는 보존한다. 컴파일러 retry와 acceptance 범위는
[어댑터 보고서](PHASE2_ADAPTER_REPORT.md)를 따른다.

정확한 어댑터 메모리 campaign 보강은 새
[진단 도구 25/1 inventory](../licenses/generated/windows-sdk-memory-campaign-release/inventory.json)와
인접 notices에 보존한다. 기존 명시적 C++ 입력 범위 안에서만 수정했고 다섯 도구의 linker
closure를 다시 검사했다. SDK 19/1, 공통 benchmark 977/22, 어댑터 37/1과 기존 SDK ZIP 감사는
`--check`로 그대로 일치한다. SDK library/public source·runtime/외부 dependency 배포는 바꾸지
않고 새 SDK ZIP도 생성하지 않는다. 원래의 메모리 gate와 Intel 액세스 위반 원인은 미해결이다.

자체 자식 process의 예외 수집은 `sdk-crash` profile과 별도
[최종 27/1 inventory](../licenses/generated/windows-sdk-crash-capture-final-release/inventory.json)를
추가한다. `Capture.cpp`/`Fixture.cpp`만 새 compiler 입력으로 허용하고 기존 다섯 도구와
수집기·fixture·심볼 포함 소비자의 총 8개 actual linker closure를 검사한다. 세 자체 PDB는
local 진단 artifact로 해시를 남기며 SDK ZIP/source archive에 재배포하지 않는다. DbgHelp는
Windows SDK/system 전제조건이다. dump는 local `out/`에만 두고 Git·배포에서 제외한다.
처음 [27/1 snapshot](../licenses/generated/windows-sdk-crash-capture-release/inventory.json)은
기존 5개 closure만 포함한 중간 snapshot이며 새 3개 closure의 증거는 최종본을 따른다.

공개 memory budget API에는 새 [SDK 19/1](../licenses/generated/windows-sdk-memory-budget-release/inventory.json),
[공통 977/22](../licenses/generated/windows-benchmarks-memory-budget-release/inventory.json),
[예산·어댑터·설치·창 39/1](../licenses/generated/windows-sdk-memory-budget-validation-release/inventory.json),
[stress 25/1](../licenses/generated/windows-sdk-memory-budget-stress-release/inventory.json),
[crash 27/1](../licenses/generated/windows-sdk-memory-budget-crash-release/inventory.json) inventory를 생성했다.
`sdk-budget`는 기존 어댑터 profile에 `MemoryBudgetTests.cpp`/설치 `budget.cpp`만 추가 허용하고
총 6개 actual linker closure와 설치 header/library 일치를 검사한다. SDK 변경으로 모든 관련
도구를 재빌드했으며 다섯 `--check`와 새 SDK ZIP 검사가 통과했다. DXGI는 기존 Windows
전제조건이며 외부 dependency나 runtime 배포는 추가하지 않았다. 과거 7개 SDK ZIP과 감사·
측정·source archives는 보존한다. 새 공개 virtual API의 소비자는 재빌드가 필요하다.

공개 memory budget을 resource-churn campaign에 연결한 변경은 새
[stress 25/1](../licenses/generated/windows-sdk-budget-campaign-release/inventory.json)과
[crash 27/1](../licenses/generated/windows-sdk-budget-campaign-crash-release/inventory.json)에 보존한다.
기존 명시적 자체 입력 범위 안의 `MemoryStress.cpp`만 변경하고 다섯/여덟 실행 파일의 actual
linker closure를 다시 감사한다. 공유 helper 소비자와 심볼 포함 stress 실행 파일은 재빌드한다.
SDK public source/library·설치본·기존 8개 SDK ZIP·외부 dependency 배포는 바꾸지 않는다.
기존 SDK/common/budget 검증 inventory와 package의 `--check`도 유지한다.

펜스 대기의 SDK 구현 변경에는 새 [SDK 20/1](../licenses/generated/windows-sdk-fence-wait-release/inventory.json),
[공통 978/22](../licenses/generated/windows-benchmarks-fence-wait-release/inventory.json),
[wait/설치/창 41/1](../licenses/generated/windows-sdk-fence-wait-validation-release/inventory.json),
[stress 26/1](../licenses/generated/windows-sdk-fence-wait-stress-release/inventory.json),
[crash 28/1](../licenses/generated/windows-sdk-fence-wait-crash-release/inventory.json) inventory를 생성한다.
private `FenceWait.hpp`는 자체 SDK 입력이며 CMake/Python fingerprint에도 포함한다.
`sdk-waits`는 budget profile에 `tests/FenceWaitTests.cpp`만 명시적으로 추가하고 7개 실제
executable linker closure를 검사한다. 전체 관련 소비자를 재빌드하고 새 SDK ZIP을 감사하며
기존 8개 ZIP·과거 감사·측정 원본을 보존한다. 외부 dependency나 runtime 배포는 추가하지 않는다.

백버퍼 수명 수정은 새 [SDK 20/1](../licenses/generated/windows-sdk-swap-guards-release/inventory.json),
[공통 978/22](../licenses/generated/windows-benchmarks-swap-guards-release/inventory.json),
[swap/설치/창 42/1](../licenses/generated/windows-sdk-swap-guards-validation-release/inventory.json),
[stress 26/1](../licenses/generated/windows-sdk-swap-guards-stress-release/inventory.json),
[crash 28/1](../licenses/generated/windows-sdk-swap-guards-crash-release/inventory.json)에 보존한다.
`sdk-swaps`는 waits profile에 `tests/SwapChainTests.cpp`만 명시적으로 추가하고 실제 8개
executable linker closure를 검사한다. SDK 수정으로 관련 소비자/도구를 다시 빌드하고 새
SDK ZIP을 감사하며 기존 9개 ZIP과 과거 근거를 보존한다. 외부 dependency/runtime는 추가하지 않는다.

복사 validation 원자성 수정은 새 [SDK 20/1](../licenses/generated/windows-sdk-copy-atomicity-release/inventory.json),
[공통 978/22](../licenses/generated/windows-benchmarks-copy-atomicity-release/inventory.json),
[copy/설치/창 43/1](../licenses/generated/windows-sdk-copy-atomicity-validation-release/inventory.json),
[stress 26/1](../licenses/generated/windows-sdk-copy-atomicity-stress-release/inventory.json),
[crash 28/1](../licenses/generated/windows-sdk-copy-atomicity-crash-release/inventory.json)에 보존한다.
`sdk-copy`는 swaps profile에 `tests/CopyAtomicityTests.cpp`만 추가 허용하고 실제 9개 executable
linker closure를 검사한다. 관련 소비자와 도구를 재빌드하고 새 SDK ZIP을 감사하며 기존 10개
ZIP·과거 감사·측정을 보존한다. 새 외부 dependency/runtime 배포는 없다.

다중 스레드 충돌 수집기 변경은 새 [crash 28/1 감사](../licenses/generated/windows-sdk-thread-snapshot-crash-release/inventory.json)에
보존한다. 기존 `sdk-crash` profile의 compiler-read 28개 자체 input, 1개 component와 8개
executable linker closure를 다시 검사한다. DbgHelp는 기존 Windows 의존성이다. SDK source와
library·기존 11개 SDK ZIP은 그대로이며 새 runtime, PDB, dump 또는 SDK ZIP을 배포하지 않는다.

고정 작업량 진단 도구는 기존 Core를 그대로 링크하며, 새
[core/helper 22개 input·1개 component 감사](../licenses/generated/windows-sdk-fixed-work-release/inventory.json)에
Core의 실제 compiler-read closure와 helper의 15개 자체 compiler input을 합친다.
실제 helper linker-read 로그의 Core·Windows SDK/MSVC library 경로와 해시, 새 executable/PDB의
로컬 해시를 기록하고 원본 MIT notices를 보존한다. 생성·해시 검증은 이번 고정 검증 스크립트에
보존하며, 기존 sdk-crash `--check`도 통과한다. 네이티브 helper는 로컬 진단용이며 Git/source ZIP에는
자체 소스·감사·검증 근거만 넣는다. 기존 SDK ZIP 11개와 과거 inventory는 수정하지 않는다.
