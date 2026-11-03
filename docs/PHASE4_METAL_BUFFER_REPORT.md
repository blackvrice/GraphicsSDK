# Metal buffers·전송 동기화·수명 준비

이 문서는 해당 slice의 당시 범위와 원본 증거다. 이후 MSL pipeline·입력/binding·draw 지원은
[Metal graphics 보고서](PHASE4_METAL_GRAPHICS_REPORT.md)를 따른다.

이 문서는 아래 `9f9b1d5` 소스의 buffer 준비와 보관 근거다. 이후 texture/pass 기능은
[Metal 텍스처 보고서](PHASE4_METAL_TEXTURE_REPORT.md)에 별도로 기록한다.

P4-MTL-03 구현 준비다. 기존 macOS opt-in Metal backend에 Upload/Device/Readback 버퍼와
논리 상태·복사·stale version·완료 보유를 추가한다. 공개 header 변경은 없다.

## 계약

- Device는 private storage, Upload/Readback은 unified memory 장치에서 shared,
  그 외에는 managed를 선택한다. 실제 storage mode·tracked hazard mode·allocatedSize·
  host mapping을 확인한다. Upload write만, Readback read만 허용한다. 크기·memory enum·
  maxBufferLength·범위/overflow·생성 thread·퇴역을 native 접근 전에 검사한다.
- managed Upload는 didModifyRange, managed GPU 복사 목적지는 synchronizeResource 후
  command completion을 요구한다. shared도 GPU 완료 전에 CPU read/write하지 않는다.
  in-flight lastUse와 성공한 CPU write/submit의 version으로 busy/stale 관찰을 거부한다.
- Device buffer의 Common/CopySource/CopyDestination, 고정 Upload source/Readback destination
  상태를 사용한다. 다른 상태는 Unsupported다. 잘못된 transition/copy는 새 자원 참조나
  관측 버전을 남기지 않는다. valid recording의 버전 관찰은 실패한 요청 이후에도 유지한다.
- macOS blit의 [4바이트 정렬 제약](https://developer.apple.com/documentation/metal/mtlblitcommandencoder/copy%28from%3Asourceoffset%3Ato%3Adestinationoffset%3Asize%3A%29?language=objc)을
  따른다. 정렬 prefix는 blit, 남은 1~3바이트만 private MSL kernel의 단일 thread가 복사한다.
  부분 복사 뒤의 논리 guard를 덮어쓰지 않는다. 내부 전송 전용 kernel이며 public compute,
  shader/pipeline API·draw 지원을 광고하지 않는다.
- copy/marker는 CPU operation list에 기록하고 end에서 새 native command buffer에 직렬
  encoder를 생성한다. 실패하면 미제출 native buffer를 버리고 CPU 기록을 재시도할 수 있다.
  submit은 모든 관측 버전을 확인하고 retention node를 확보한 뒤 commit하며 version/state를
  갱신한다. 실패한 제출은 논리 상태·version·fence sequence를 진행시키지 않는다.
- pending은 native buffer/allocations를 보유하고 public wrapper를 보유하지 않는다.
  recording도 wrapper와 별도로 allocations를 보유한다. statistics는 실제 wrapper와
  unique allocation의 MTLResource allocatedSize, pending 제출/참조를 집계한다. 드라이버
  cache·residency·OS memory budget·프로세스 전체 사용량이 아니다.
- Device 파괴는 pending 완료를 기다리고 weak registry의 native recording/allocation과
  queue/device/cache를 해제한다. 남은 자식은 CPU description만 유지하고 native 조작은
  DeviceLost다. texture·draw·timestamp·표시·native validation·recovery는 미지원이다.

## 실행과 증거

MetalBufferTests는 1/2/3/4/5/7/257/259/4096/65539/1048579바이트의 full copy와 각 19바이트
guard를 가진 partial copy를 검사한다. Upload→private Device→Readback의 전체 byte 참조,
동일 command의 write-after-write, unaligned CPU slice, 잘못된 range/memory/state/thread/
generation·stale CPU write/빈 write·sibling 제출·transition, 연속 제출의 private 데이터,
거부 원자성·미제출 보유·
wrapper 해제 후 native 보유·최종 회수·퇴역을 검사한다. busy/timeout 강제 주입은 하지 않는다.

macOS CI는 CPU-only Core/가용성 조사 후 Metal ON CTest 13개와 submission/buffer 직접
실행 및 두 설치 소비자를 수행한다. 직접/설치 buffer 실행은 각 22개 실제 전체 readback
파일을 저장하고 Windows/Linux에서도 현재 검사기로 모든 byte/guard를 재계산한다.
7개 추가 Python 계약은 누락·변조·scope 승격·compiler graph·skip을 거부한다.

`macos-metal-buffers-v2` receipt는 이전 submission v1과 분리한다. v1 source ZIP·compiler
graph·12 CTest·201 marker 제출은 기존 범위로 계속 재생하고 새 buffer 증거로 승격하지 않는다.
새 receipt는 source/installed header/compiler graph·cache·native/Python receipt와 원본
stdout/stderr·PID/직렬 시간·전체 payload를 보관한다. binary/system SDK는 보관하지 않는다.

```sh
python3 tools/macos_metal_backend.py run --availability out/macos-metal-evidence --output out/macos-metal-backend-evidence
python3 tools/macos_metal_backend.py check --output out/macos-metal-backend-evidence --python out/macos-python --live
```

물리 GPU·성능·native validation/오류 강제 주입과
선행 Phase 3/P4 수용을 완료한 것으로 승격하지 않는다. 143개 roadmap 상태는 유지한다.

## 실제 실행과 보관 결과

소스 `9f9b1d50a9c818c174a9350a072ae6707e9fe4ed`, SDK fingerprint
`7c795e6f5dad1cf9f6bf97015bf47c84153e510b1d816bd6f9de911b8b55dbbe`의
[macOS arm64/Intel CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41616526930)가
모두 통과했다. 각 환경에서 CPU 11+Metal 2 CTest, marker/buffer 직접 실행과 두 설치
소비자, 전체 Python 1,487개가 통과했다. buffer 직접/설치 실행은 각각 151개 assertion,
full 11+partial 11개의 모든 byte와 19-byte guard를 검사했다. 별도 CTest 실행도 같은
payload 비교와 제어 계약을 수행한다. 이전 marker suite의 201회 제출과 buffer 제출은
구분하며 201을 전체 buffer 제출 수로 재사용하지 않는다.

각 환경의 직접/설치 readback 파일 44개, 4,475,426바이트를 저장하고 Windows의 현재
checker로 모든 byte/guard를 독립 재계산했다. 총 88개·8,950,852바이트다. 두 장치는
Apple Paravirtual device이며 arm64의 unified_memory=true, Intel=false를 관측했다.
native buffer factory는 선택한 shared/managed·private mode와 tracked hazard/mapping/
allocatedSize를 검사했다. 이로써 두 host storage 경로의 실제 데이터 전송을 확인했다.
물리 GPU·실제 discrete 장치 driver 수용으로 승격하지 않는다.

같은 소스의 [Windows/Linux 회귀 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41616527013)도
통과했고 [Linux Vulkan 컴파일·CPU 계약](https://github.com/blackvrice/GraphicsSDK/actions/runs/41616527020)도
통과했다. source ZIP 8개 입력, SDK/tests compiler graph 19 units 중 private ObjC++/ARC 1개,
설치 소비자 2 units, public headers 13개를 기록했다. 원본 evidence 파일 320개를
`benchmarks/reports/validation/2026-11-03-metal-buffers/`에 inventory/summary/manifest와
함께 보관했다. evidence ZIP은 1,114,003바이트다. native executable·system SDK/framework는
재배포하지 않으며 hash/크기와 실제 source/build/receipt/전체 readback 데이터만 보관한다.
manifest는 자체 hash 목록으로 외부 신뢰 서명이 아니다. archived code/native binary를
실행하지 않고 현재 checker로 재생한다.

```sh
python3 benchmarks/reports/validation/2026-11-03-metal-buffers/verify.py
```

이 재생을 Windows/Linux/macOS CI에 연결한다. 과거 submission v1, Metal availability,
Core 및 Linux 증거도 유지한다. P4-MTL-03의 native API 기능 준비 근거이며 선행 gate와
물리 GPU·native validation/오류 주입 수용은 미완료다. 다음 구현은 P4-MTL-04의 textures·
buffer-texture 전송·pass이며 별도 전체 texel 참조가 필요하다.
