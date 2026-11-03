# macOS Core 빌드·CPU CI 준비

2026-11-03. macOS 실행 환경을 GitHub Actions로 확보해 공통 Core를 검증한다.
P4-MTL-01/09의 준비 근거이며 Phase 3 gate·Metal/GPU 수용 완료를 의미하지 않는다.

`.github/workflows/macos-core.yml`은 macos-15 arm64와 macos-15-intel x86_64를
독립 실행한다. Apple clang/Xcode/macOS SDK·OS·CMake/Ninja 원본 출력을 보관하고
실제 process architecture, 컴파일의 -arch/-isysroot, 생성 library/실행 파일의 lipo
결과를 일치시킨다. 일반 macOS runner를 사용하며 GPU larger runner를 생성하지 않는다.

`tools/macos_core.py`는 clean same-revision checkout에서 native Darwin/architecture를
요구한다. Release O2/NDEBUG, IPO/unity OFF, D3D12/Vulkan/XCB/validation OFF,
tests/examples ON으로 구성한다. 공통 CPU CTest 11개 전체를 실행하며 누락·중복·실패·
skip·잘못된 시간값은 거부한다. GPU suite를 제외한 결과를 GPU 실행으로 보고하지 않는다.

Core 5개 source, CPU tests 11개, examples 2개의 compile graph 18 units를 검사한다.
이는 CMake compiler command graph 및 원본 verbose build 로그 검증 범위이며 전체
system header/linker 입력·라이선스 감사나 GPU validation을 대신하지 않는다.

별도 `tests/macos_core_consumer`는 fresh installation의 find_package와 public header,
정적 library만 사용한다. 설치 header 전체의 원본 hash와 SDK source fingerprint를
확인하고 layout 검증 및 세 미컴파일 backend의 명시적 Unsupported 응답을 검사한다.
이 소비자 성공은 P4-MTL-09의 향후 실제 Metal 실행·전체 패키징 수용을 완료하지 않는다.

전체 Python 계약을 제외 없이 실행하고 source commit·SDK fingerprint·Darwin/architecture·
실제 검사한 runner 코드·원본 로그 hash를 native receipt와 대조한다. 9개 추가 CPU 계약은
CTest 누락/실패/skip, compiler target/SDK/backend 변조, Python 결과·source·로그·runner
code 변조와 잘못된 host/architecture·GPU 수용 승격을 거부한다. 합성 테스트는 실제
macOS 실행 증거가 아니다.

```sh
python3 tools/macos_core.py run --architecture arm64 --output out/macos-core-evidence
python3 tools/check_python_contracts.py --output out/macos-python
python3 tools/macos_core.py check --output out/macos-core-evidence --python out/macos-python --live
```

Intel host에서는 architecture를 x86_64로 지정한다. 두 CPU 아키텍처를 실행하는 CI와
offline check는 구분되며 외부 시스템/native binary는 재배포하지 않는다. source ZIP,
원본 commands/cache/graph/CTest XML·consumer 출력, 설치 header/native artifact hash와
source 불변 receipt를 보관한다. GPU 존재 여부는 이 도구에서 조회하지 않는다.

Metal backend·장치/queue·buffer/texture/draw·표시·native validation·경쟁 성능 비교는
Phase 4의 별도 개발이다. P3-LNX-15의 Linux 물리 GPU 수용 역시 미완료로 유지한다.
