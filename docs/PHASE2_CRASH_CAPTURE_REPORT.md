# Phase 2 자식 process 예외·스택 수집

2026-10-22. 직전 Intel 장시간 실행은 `0xc0000005`, faulting module `unknown`과 불완전한
workload footer만 남겼다. 이번에는 SDK와 별개인 **새 자식 전용 Windows x64 debugger**를
구현했다. 수집기 검증은 통과했고, 새 심볼 포함 Intel 계측 실행은 크래시 없이 끝났다.
**직전 크래시 원인은 미확정이며 원래 장시간 메모리 gate도 미완료다.**

SDK 공개 API/구현/library와 기존 메모리 policy는 바꾸지 않았다. SDK fingerprint는
`85190ccf45a1c6c57473c25c8bef5c69e5387a5631a742d5561aef8b0e659970`이다.

## 개발한 진단 경로

`tools/crash_capture`는 Windows SDK/DbgHelp만 링크하는 전용 CMake 프로젝트다.
`gfx_crash_capture`는 `CreateProcessW` + `DEBUG_ONLY_THIS_PROCESS`로 **자신이 생성한 자식**만
디버깅한다. 기존 process attach, WER/TDR/driver/전역 debugger 정책 변경, symbol server는 없다.
초기 loader breakpoint 한 번을 처리하고 다른 first-chance 예외는 자식에게 전달한다.
second-chance 예외를 삼키거나 exit 0으로 바꾸지 않는다. watchdog은 자식을 종료하고 reap한다.
debugger 비정상 종료 때도 자신이 생성한 자식의 종료 정책을 유지한다.

JSONL에는 launch/예외 코드·주소·매개변수·thread ID/로드 모듈 image range/CONTEXT/최대 64개
fault-thread stack frame/실제 exit와 timeout·dump·수집 상태를 기록한다. 로컬 PDB가 없으면
모듈·offset 또는 null로 남긴다. RIP=0이면 fault module을 추측하지 않고 스택 첫 항목을
`null_pc_stack_top_candidate`로 구분한다. 후보 호출자만으로 잘못된 포인터의 생성 원인을
특정할 수는 없다. 실제 null 호출 대조에서는 PC 0과 호출자 `wmain` 후보를 확인했다.

debug text는 [byte-length 계약](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-output_debug_string_info)을
따라 알린 바이트 밖을 읽지 않고 첫 NUL에서 자른다. UTF-16 오류는 표시하고 대체 문자로
변환하며, 선택적 문자열 변환 때문에 예외 수집을 종료하지 않는다. 전체 read budget은 64KiB,
별도 message count는 생략된 text도 센다. 이는 SDK의 unfiltered 오류 집계를 대신하지 않는다.

`--dump`는 second-chance에 normal/thread/unloaded-module minidump를 선택적으로 쓴다.
fixture dump는 local `out/`에 보존하고 해시만 기록했다. `.dmp`, `.pdb`, 실행 binary는 Git 증거·
source archive·SDK ZIP에 넣지 않는다. dump/capture 기존 파일을 거부하고 실패한 dump는 성공
수집으로 인정하지 않는다. capture의 complete는 process 관측 완료이며 메모리 합격이 아니다.

`gfx_memory_stress_symbols`는 기존 `MemoryStress.cpp`를 Release `/O2 /MD` + `/Zi`, linker
`/DEBUG`로 빌드해 기존 core library에 링크한다. 자체 workload PDB만 추가되며 SDK/runtime/driver
private PDB를 만들었다는 뜻은 아니다. `capture_memory_failure.py`는 정확한 public adapter ID,
full GBV/state tracking, 기본 512MiB guard를 유지하고 source/exe/PDB/core 입력을 전후 고정한다.
completed/guard_aborted/timed_out/crashed/capture_failed를 구분하며 **모든 결과의
`memory_acceptance`는 false**다. disk module hash도 process 종료 후 파일 관측이라고 명시한다.

## 실제 실행과 판정

독립 native control 일곱 종류가 통과했다: 처리된 예외, 빈 값·한글·따옴표·후행 역슬래시 인자와
정확한 Unicode debug text, exit 7, 실제 null 호출 AV, 애플리케이션 breakpoint, timeout/reap,
1,000개 debug message의 text 한도. 추가 기존 출력 덮어쓰기 거부도 통과했다.
null AV와 애플리케이션 breakpoint는 실제 second-chance와 원래 비정상 exit를 남겼다.
이 의도적인 실패들은 정상 GPU 실행의 ERROR 0과 구별한다.

Intel UHD 770, ID 106703의 새 계측 process는 30초 warmup 뒤 **300.106초**를 완료했다.
총 17,696 verified frames / 53,088 full images / 4,638,900,224 buffer bytes가 기준과 일치했다.
마지막 SDK 보유 다섯 값과 활성 세대의 SDK ERROR/CORRUPTION은 0이었다. collector는 child
exit 0, first-chance 3개, **second-chance 0개**, timeout 없음, complete=true를 기록했다.
53,103개의 debug message 중 text read 65,374 bytes를 보존했다. 예외가 없으므로 Intel dump는
생성하지 않았다. 앞선 112.875초 AV는 **이번 한 process에서 재현되지 않았을 뿐 해결되지 않았다.**

같은 계측 JSONL을 기존 policy 분석기로 검사하면 `Memory plateau gate failed: private_bytes`로
거부된다. 이 관측도 debugger/새 executable에 한정하며 원래 정상 campaign의 새 합격으로
승격하지 않는다. NVIDIA/Intel/WARP의 별도 2초 exact-ID 계측도 전체 참조와 complete footer를
통과했으나 메모리 합격은 false로 유지한다. 시간·처리량의 기존 실행 대비 성능 비교는 하지 않는다.

## 발견·보존한 한계와 회귀

초기 수집기는 Unicode debug-string 길이를 문자 수로 취급했다. 그래픽스 실행의 optional text
변환 오류로 수집이 거부됐으며 byte 계약·NUL 처리·치환 변환으로 수정했다. 원본은 local `out/`
격리 경로에 두고 publication evidence에서는 제외한다. 수집이 불완전한 초기 실행을 현재의
크래시/메모리 증거로 표현하지 않는다. 최종 native text 대조는 substring이 아닌 정확한 내용도
검사한다. 초기 검증 스크립트의 'Intel 실행이 실패할 것'이라는 가정도 최종 실제 결과로 교정했다.

첫 직렬 CL.exe 빌드에서 AV(-1073741819)가 한 번 발생하고 같은 소스/옵션의 retry는 통과했다.
최종 빌드는 별도 기록했다. 작업 시작의 Git status도 한 번 entry-format 오류를 냈지만 원본
index checksum은 유효했고 3,509개 blob/mode/stage가 모두 HEAD와 같았다. 인덱스 교체 없이
재조회와 read-only fsck가 통과했다. 두 현상의 원인을 SDK·driver·hardware로 단정하지 않는다.

최종 Core CTest **25/25**, Python **147/147**이 통과했다. 새 Python 8개는 불완전한 context/
exit, 예외 누락, breakpoint 잘못 처리, text 한도, scope 변경, dump/timeout 불일치를 거부한다.
실제 native controls와 합성 parser tests는 별도다. Debugger 관측은 검증되지 않은 실제
TDR/hot-unplug, 육안 표시/DPI, Linux/Vulkan/macOS/Metal을 대신하지 않는다.

최종 compiler-read 감사는 27 inputs/1 own component, actual linker closure 8개다.
중간 snapshot의 기존 5개 closure와 구별한다. 기존 SDK 19/1, stress 25/1, 공통 977/22,
어댑터 37/1 및 기존 SDK ZIP 검사는 그대로 일치했다. SDK source/library/7개 기존 SDK ZIP과
과거 실패/원본/source archives를 보존한다. common/presentation의 기존 결과는 해시 대조만
수행하며 이번 새 GPU 검증으로 재분류하지 않는다. Sandbox/경쟁 SDK를 변경하지 않았다.

[사용 방법과 API 출처](../tools/crash_capture/README.md),
[최종 원본](../benchmarks/reports/validation/2026-10-22-phase2-crash-capture-final/),
[초기 실패 메타데이터](../benchmarks/reports/validation/2026-10-22-phase2-crash-capture/),
[요약·해시·소스 보존](../benchmarks/reports/2026-10-22-phase2-crash-capture/)를 함께 확인한다.
