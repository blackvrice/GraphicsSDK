# 배포 의존성 라이선스 감사

조사일: 2026-10-20. 범위는 Windows x64 Release SDK, LLGL/SDL3 기본 벤치마크와
추가 standalone Diligent Core 벤치마크 profile이다.
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

고지 모음은 배포 시 사용할 자료이며 `inventory_verified`는 법적 배포 승인 표시가 아니다.
실제 배포 구성, SDK/백엔드/옵션/버전, 추가 라이브러리 또는 시스템 DLL 포함 범위가 달라지면
다시 검토한다. Python plotting 패키지는 도구 전용이며 현재 배포 대상에서 제외했다.
모든 미래 경쟁 SDK의 전이 의존성을 검토 완료했다고 주장하지 않는다.
