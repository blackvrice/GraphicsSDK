# Linux 세 SDK allocation 반복 캠페인

2026-11-03. P3-LBEN-04의 Linux 반복 실행기 연결이다. 실제 Linux GPU의 fresh 성능
수용은 미완료이며 software 진단을 성능 비교로 승격하지 않는다.

## 고정 계약

Windows `llgl-allocation-comparison.json`의 buffer1/buffer1000/texture1/texture1000,
4096바이트 버퍼·65×37 RGBA8 텍스처, public 생성·해제 두 CPU 구간을 유지한다.
descriptor/payload 준비, staging 생성·해제, GPU 복사·fence/idle, 전체 readback,
teardown과 warmup/validation은 통계 밖이다. SDK general device buffer, SDL vertex
buffer, LLGL vertex/copy buffer의 public usage 차이를 보존하며 전체 SDK 순위를
만들지 않는다. GPU timestamp, allocator cache 상태, native allocation 개수와 실제
GPU 메모리 회수는 확인하지 않는다.

고정 LLGL 소비자의 전체 Allocation 구현과 반복 루프를 보존한다. Linux UUID/null
LUID·환경·실제 module 조회·공유 support/OpenSSL만 이식하며 승인된 10줄 patch를
변경하지 않는다. 모드와 source hash를 fingerprint에 결합하고 생성 파일은 `out/`
아래에만 쓴다. GNU 타이머는 O2/NDEBUG, IPO/unity OFF로 빌드한다.

```sh
python3 tools/linux_allocation_campaign.py smoke --readiness out/linux-workloads-evidence --llgl-correctness out/linux-llgl-allocation-evidence --output out/linux-allocation-campaign-smoke
python3 tools/linux_allocation_campaign.py check --output out/linux-allocation-campaign-smoke --live
```

물리 `run`은 같은 source/qualification의 SDK/SDL readiness와 완료된 LLGL allocation
correctness를 요구한다. guard-only/software 입력을 거부하고 SDK·SDL·probe·소비자의
`gnu-release-o2-no-ipo-unity-v1` 원본 cache·compiler graph를 검사한다. 고정 VVL과
명시한 native UUID/kind가 필요하며 다른 adapter로 재시도하지 않는다. physical 수동
workflow에 연결하되 실제 GPU runner가 없어 이번에는 실행하지 않는다.

## 측정 전 gate와 관측

세 SDK probe, LLGL 관측 probe와 네 입력 native 감사, 세 SDK required VVL 검증,
SDK별 누락 layer·잘못된 UUID/kind, LLGL core/sync 전달·sync 비활성, 누락/별칭 자원,
해제 누락, 잘못된 자원 payload·stale iteration·copy 누락·손상/불완전 readback·
observer 누락을 검증한다. 선행 41개 프로세스의 gate 중 하나라도 실패하면 후속
측정에 진입하지 않는다. 지정한 오류·종료 코드 1·빈 stdout을 요구하는 거부 19개와
core/sync 양성 대조 2개를 포함한다.

모든 성공 workload는 전체 자원 바이트를 확인한다. LLGL 관측/required validation은
4개 iteration × 모든 resource의 hash를 독립 CPU 참조와 비교하고 실제 VkBuffer/
VkImage 핸들과 live unique resource 수를 확인한다. 이 참조는 CPU payload hash
상수이며 과거 GPU 결과나 타이밍을 재사용하지 않는다. copy workload의 native draw는
0이어야 한다. native 자원 핸들 수는 driver 메모리 allocation 개수가 아니다.

plain 측정에는 observer/VVL/loader trace가 없고 draw 관측 계수는 null이다. native
자원 핸들 확인과 전체 readback은 유지한다. SDK/SDL plain module snapshot과 SDL
native handle 미관측 한계를 유지한다. SDK/SDL 명시적 instance API 1.1과 LLGL의
별도 관측한 요청 값도 구분한다.

## 반복과 증거

물리 계약은 네 입력 × 세 SDK × 다섯 독립 프로세스, warmup 5회·측정 30회다.
입력별 서로 다른 다섯 SDK 순열을 인접 실행하며 seed로 입력 block 순서를 섞는다.
총 프로세스 101개(선행 41 + 측정 60), 기본 표본 1,800개와 같은 표본의 두 CPU 열
통계 24행이다. 모든 표본/outlier와 process median을 보존하며 제거 표본은 0이다.
전체 이미지/버퍼 각각 541,541개·총 7,427,776,356바이트는 실행 시 요구할 수치이며
이번 물리 측정 결과가 아니다. 좁은 CPU 비교가 성공해도 P3-LBEN-04/Phase 3 또는
전체 물리 GPU 수용을 자동 완료하지 않는다.

software 계약은 53개 프로세스, 전체 이미지/버퍼 각각 28,028개·총 자원 56,056개·
384,432,048바이트다. 기본 진단 행 84개를 보존하며 측정 표본·통계·성능 수용은 0이다.
공통 실행기에 버퍼/전체 자원 추가 계수의 누적·재생을 연결하고 기존 upload/draw/PSO
기본 동작을 유지한다. 각 create/public-release 구간 합이 enclosing process 시간
안에 들어오는지도 검사한다.

source ZIP·GNU verbose/Ninja/depfile·license 분류·생성 adapter/fingerprint·실제
native module/file hash·원본 stdout/stderr·유한 직렬 실행 증거를 보관·재생한다.
추가 CPU 계약 테스트 15개는 고정 계약·전체 자원 계수·payload·native 핸들·staging
정책·시간값·관측 분리·정확한 오류·물리 prerequisite·통계·범위 변조와 생성기 보존을
검사한다. 합성 테스트는 실제 GPU 실행 증거가 아니다.

## 남은 수용

실제 Linux GPU의 qualification과 같은 장치·driver에서 네 workload의 fresh 비교,
실제 표시 검증과 Phase 3 최종 gate가 남아 있다. Intel 충돌·장시간 메모리 증가
조사를 재개하지 않는다.
