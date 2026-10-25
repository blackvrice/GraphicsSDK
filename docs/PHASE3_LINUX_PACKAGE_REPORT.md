# Phase 3 — Linux 설치 소비자·입력·배포 파일 감사

2026-10-25 · `P3-LNX-12` · 검증 소스 커밋 `199812f`.

실제 Ubuntu에서 SDK를 설치한 뒤 경로를 옮기고, **저장소 밖의 별도 프로젝트가 옮겨진 설치본만 사용해 소비자 9개를 빌드·실행**했다. GNU 컴파일러 의존성, 링커 trace/map, 정적 라이브러리의 실제 object, 설치 파일과 라이선스 고지, 최종 ZIP의 바이트를 검사했다. Vulkan 공통 suite 8개의 **2,666 assertion**과 감사 도구의 대조 테스트 **13개**가 통과했다.

[설치 감사 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40187751547), [job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40187751547/job/121152044480), [요약](../benchmarks/reports/2026-10-25-phase3-linux-package/summary.json), [원본 receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-package/native/receipt.json), [로그](../benchmarks/reports/validation/2026-10-25-phase3-linux-package/job.log)에 근거를 보존한다. SDK C++·공개 API·root `CMakeLists.txt`는 변경하지 않았다.

## 저장소 밖 소비자와 설치 경로 이동

[`linux_package.py`](../tools/linux_package.py)는 `linux-vulkan` preset의 Release SDK만 별도 경로에 빌드한다. D3D12 OFF, Vulkan ON, tests/examples OFF, g++-14/Ninja를 사용한다. `cmake --install` 후 고지를 추가하고 설치본을 `out/linux-package-relocated`로 이동한다. 원래 설치 경로는 사라지고, SDK 빌드 경로도 별도 퇴역 경로로 옮겨 원래 경로가 없음을 확인한다. 원본 소스·퇴역 라이브러리는 감사 비교를 위해 남겨 둔다.

실제 소비자 프로젝트는 `/tmp/graphics-sdk-consumer-aj1px3rg`에 만들었다. 공통 suite와 필요한 자체 fixture/support를 원본과 바이트 대조해 복사했으며 SDK 소스·비공개 헤더·Vulkan 헤더는 복사하지 않았다. 전용 [CMake 프로젝트](../tests/linux_package_consumer/CMakeLists.txt)는 `find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`와 `GraphicsSDK::Core`를 사용한다. 사용자/system package registry를 끄고 옮겨진 prefix를 지정했다.

설치 소비자 9개의 실제 의존성에는 설치 공개 헤더 13개, 복사한 자체 소비자 입력 16개, 시스템 헤더만 있었다. 저장소의 `include`/`src`, 원래 build/install 경로, `.deps/Vulkan-Headers`가 소비자 컴파일 입력에 들어가지 않았다. 링커 9회 모두 옮겨진 `libGraphicsSDKCore.a`와 각 소비자 object 하나를 사용했다. Exported CMake 파일에도 저장소·빌드·원래 설치 경로가 없었다.

| 설치 소비자 | Assertion | 실행 범위 |
|---|---:|---|
| Buffer·queue/fence·수명 | 361 | software Vulkan |
| Texture 전송·padding | 418 | software Vulkan |
| 컬러 pass·MRT | 297 | software Vulkan |
| Draw·blend/scissor·instancing | 319 | software Vulkan |
| Vertex/index/instance | 386 | software Vulkan |
| Binding·sampler·snapshot | 287 | software Vulkan |
| D32·depth·MRT | 442 | software Vulkan |
| Timestamp·availability·수명 | 156 | software Vulkan |
| Generic CPU consumer | 별도 exit 0 | version·binding·render-target 계약 |
| Vulkan 합계 | **2,666** | 8회 실행 모두 통과 |

Generic CPU 소비자는 D3D12가 빌드되지 않은 분기에서 정상 종료했다. 그 뒤의 D3D12 GPU/resource statistics/recovery 코드는 실행하지 않았으며 Linux GPU 성공으로 합산하지 않는다.

## 실제 GNU 입력 수집과 감사

[`linux_build_capture.py`](../tools/linux_build_capture.py)를 CMake의 CXX compiler/linker launcher로 연결했다. 최초 compiler-ID/ABI configure 뒤 launcher를 지정하고 실제 SDK·소비자 target을 빌드한다. Configure probe까지 입력 폐쇄성을 증명한다고 주장하지 않는다.

- **컴파일 15회**: SDK 6개, 소비자 9개. 실제 argv/cwd/exit code/object SHA-256과 시스템 헤더를 포함한 `-MD -MF` depfile을 Ninja가 제거하기 전에 보존한다. `-MMD`는 거부한다. [GCC의 의존성 옵션 정의](https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html)를 따른다.
- **링크 9회**: 실제 driver 호출에 `-Wl,-t,-t`와 별도 map 경로를 추가해 trace·archive member 추출·map·ELF 해시를 보존한다. [GNU ld 정의](https://sourceware.org/binutils/docs/ld/Options.html)에서 두 번의 trace는 archive member도 출력한다.
- **SDK archive**: `ar t`의 고유 member 6개와 `ar p`의 바이트 해시가 실제 컴파일된 SDK object 6개와 정확히 일치한다. 설치·이동·패키징 후 같은 archive인지 재검사한다.
- **입력 허용 목록**: [Linux 정책](../licenses/linux-vulkan-policy.json)의 자체 파일과 고정 Vulkan 헤더만 SDK에 허용한다. 소비자는 옮겨진 설치 헤더와 복사된 자체 소비자 파일만 허용한다. 시스템 입력은 별도 분류하고 SHA-256·실제 `dpkg-query -S` 소유자를 기록한다. `/usr/lib`와 `/lib`의 배포판 경로 별칭 조회도 원본 기록에 남긴다. 알 수 없는 외부 입력은 실패한다.

최종 고유 입력 기록은 451개다: 자체 SDK 입력 24, Vulkan 헤더 11, 복사 소비자 입력 16, 설치 공개 헤더 13, 소비자 object 9, 설치 archive 1, 시스템 컴파일 입력 360, 시스템 링크 입력 17. 이는 실제 compiler dependency와 linker trace의 보수적 집합이다. 모든 헤더가 최종 machine code에 기여했다거나 전체 symbol 도달성을 증명한다는 의미는 아니다. 시스템/toolchain 파일의 포괄적 법률 판단도 포함하지 않는다.

## 라이선스 고지와 최종 설치 파일

원래 CMake 설치는 자체 MIT `LICENSE`를 제공한다. **이번 검증 도구가 설치 후 패키징 단계에서 `share/GraphicsSDK/THIRD_PARTY_NOTICES.txt`를 추가**한다. 일반 `cmake --install` 자체에 새 고지 설치 기능을 구현한 것으로 표시하지 않는다. 고지는 자체 MIT 전문, 실제 컴파일 입력으로 확인한 Vulkan 헤더 11개의 저작권/SPDX 문구, 고정 Vulkan-Headers의 Apache 2.0 전문으로 구성한다. [보존된 고지](../benchmarks/reports/validation/2026-10-25-phase3-linux-package/native/THIRD_PARTY_NOTICES.txt)를 패키지 파일과 바이트 대조했다.

고정 Vulkan-Headers commit은 `b379292b2ab6df5771ba9870d53cf8b2c9295daf`다. 최초 실행 `12781ea`의 [실패 로그](../benchmarks/reports/validation/2026-10-25-phase3-linux-package/failed-12781ea-job.log)는 기존 Windows CRLF 정책 해시와 Linux LF 원본의 차이를 검출했다. `199812f`에서는 **정확한 고정 Git blob 해시와 기존 Windows CRLF 해시를 별도 필드로 보존**했다. 11개 모두 Git 원본의 CRLF checkout 바이트로 기존 Windows 해시를 재현했다. 임의로 줄바꿈을 정규화해 변경된 헤더를 통과시키지 않는다. 과거 Windows 정책·결과는 그대로다.

최종 설치본과 [검증용 SDK ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-package/native/GraphicsSDK-linux-verification.zip)은 정확히 **20개 파일**이다.

| 파일 종류 | 개수 |
|---|---:|
| `include/graphics/*.hpp` | 13 |
| `lib/libGraphicsSDKCore.a` | 1 |
| `lib/cmake/GraphicsSDK/*.cmake` | 4 |
| MIT LICENSE·THIRD_PARTY_NOTICES | 2 |

공개 헤더는 Git 원본과, archive는 실제 빌드 object/기존 native baseline과, ZIP entry는 감사된 설치 파일과 각각 일치한다. 누락·추가 파일·symlink를 거부한다. Vulkan 헤더·loader·ICD·VVL·vendor source·소비자 executable·시스템 라이브러리는 SDK ZIP에 포함하지 않는다. ZIP은 144,349 bytes이며 SHA-256은 `0f5b3f6f0c6b6dc9728ab49da15237df8fe8c446b803b7a54ff57d8ff7d598f1`이다. Phase 7의 정식 다중 플랫폼 배포 수용을 대신하지 않는 검증용 payload다.

[`test_linux_package_audit.py`](../tests/test_linux_package_audit.py)의 13개 테스트는 GNU 줄 연속/공백 escaping, 누락 dependency/target, 알려지지 않은 입력, 소비자의 저장소/vendor 헤더 혼입, 정상 설치 헤더, 누락/변조 notice, 추가 runtime binary, 변조 공개 헤더, 오래된 archive, 정확한 ZIP을 검사한다. 이 대조들은 합성 파일을 사용한 감사 도구 테스트이며 실제 컴파일 읽기 기록으로 합산하지 않는다.

## 실행 환경·재현과 수용 경계

Ubuntu 24.04.5 x86_64, kernel `6.17.0-1022-azure`, g++ `14.2.0-4ubuntu2~24.04.1`, binutils `2.42-4ubuntu2.10`, loader `1.3.275.0-1build1`, Mesa `25.2.8-0ubuntu0.24.04.4`를 관측했다. 장치는 `llvmpipe (LLVM 20.1.2, 256 bits)`, native `PHYSICAL_DEVICE_TYPE_CPU`, SDK `software=1`, adapter ID 문자열 `5892941257450743403`이다.

Child 환경에만 하나의 lavapipe ICD·빈 explicit layer 경로·implicit layer 비활성화·`LP_NUM_THREADS=2`를 지정했다. 8개 suite의 `LD_DEBUG=libs`에 실제 `libvulkan_lvp.so` 로드가 있고 Khronos validation layer 로드는 없다. `readelf -d`에는 Vulkan/VVL/ICD의 `NEEDED` 항목이 없다. 외부 Vulkan runtime은 실행 시 동적으로 로드하며 SDK ZIP에 묶지 않는다. 이번 실행은 모두 **`validation=0`**이며 [P3-LNX-11의 별도 고정 VVL 수용](PHASE3_LINUX_VALIDATION_REPORT.md)을 반복·대체한 것으로 표시하지 않는다. SDK archive는 그때와 바이트가 같다.

```text
Linux SDK source fingerprint (27 Git/LF files)
ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830
Windows source fingerprint (same files, CRLF checkout)
2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401
Linux Core archive: 413,918 bytes
c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32
GitHub raw artifact ZIP: 416,345 bytes
d3cd121f2f4f7677e922fd5fdf8a369daa2300ea676c8443178ae7bed2f2073e
```

원본 artifact를 내려받아 GitHub digest, 모든 receipt diagnostic, 검증한 Git 입력, 고정 헤더/라이선스 원본, 실제 depfile/trace/map와 object/archive, 최종 ZIP entry를 다시 대조했다. 현재 Windows SDK fingerprint와 메모리 정책 해시도 이전 값 그대로다.

고정 Vulkan-Headers checkout과 전체 저장소가 있는 환경에서는 보존된 [원본 대조 도구](../benchmarks/reports/validation/2026-10-25-phase3-linux-package/verify_evidence.py)를 실행해 이 해시·입력·패키지·실행 결과와 전체 CI 오류 목록의 동일성을 다시 확인할 수 있다. 원격 artifact 보관 기간이 지나도 저장소에 보존한 ZIP과 원본 로그를 사용한다.

```sh
python benchmarks/reports/validation/2026-10-25-phase3-linux-package/verify_evidence.py
```

재현은 새 Linux checkout에 고정 Vulkan-Headers를 `.deps/Vulkan-Headers`로 준비하고 필요한 GNU/Ninja/Vulkan runtime을 설치한 뒤 실행한다. 출력·build/install 경로가 이미 있으면 실패하므로 기존 근거는 보존하고 새 checkout을 사용한다.

```sh
python3 -m unittest discover -s tests -p test_linux_package_audit.py -v
python3 tools/linux_package.py
```

[같은 SDK 소스의 compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40187584233)는 두 preset의 CPU 22개를 통과했다(소스 커밋 `12781ea`; 후속 변경은 감사 도구·정책뿐). [전체 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40187751574)는 C++/설치/Windows 감사 단계는 통과했으나 Python **627개 중 Linux errors=92, Windows errors=99**가 남는다. 새 감사 테스트 13개는 두 플랫폼에서 통과했고 기존 플랫폼/고정 자료·경로 별칭 문제는 `P3-LNX-14`에 유지한다. 전체 CI가 성공했다고 표시하지 않는다.

`P3-LNX-12`는 이번 설치·입력·고지·payload 범위에서 완료다. 물리 GPU의 공통 suite/validation 수용은 최종 gate의 필수 의존성 `P3-LNX-15`, Linux WSI는 `P3-WSI-01`~`08`, 전체 CI 정비는 `P3-LNX-14`에 남는다. 장시간 메모리·Intel 충돌 조사는 사용자 지시대로 보류하며 원인을 하드웨어로 확정하지 않는다.
