# Linux LLGL PSO 타이머 소비자 이식

2026-11-03. P3-LBEN-03의 타이머 준비 단계다. 세 SDK PSO 캠페인과 실제 GPU 성능
수용은 미완료이며 로드맵 상태를 완료로 변경하지 않는다.

고정 `llgl_vulkan_pso_compare`의 `PSO` 본문 전체와 반복 루프를 그대로 보존한다.
생성 시간에는 public pipeline 및 shader 객체 생성·해제가 포함되고, 해제 시간은
public pipeline handle 해제다. descriptor 준비, GPU 사용·fence/idle, 전체 이미지
검사, teardown, warmup은 측정 밖이다. driver cache 상태, native pipeline 개수와
실제 GPU 메모리 회수는 확인하지 않는다.

플랫폼 변경은 Linux UUID/null LUID·환경·실제 module 조회·공유 support와 OpenSSL에
한정한다. 기존 승인 LLGL 10줄 patch와 원본 observer를 유지한다. 소스 hash와 모드가
fingerprint에 포함되고 physical/software 생성 결과는 구분된다. GNU Release는
`-O2 -DNDEBUG`, IPO/unity OFF다. 생성 파일은 저장소 `out/` 아래에만 쓴다.

## 검증

기존 Linux Docker 검증 환경에서 GCC 14로 실제 소비자와 observer를 빌드했다.
plain/observe probe와 pso1/pso8의 짧은 실행, 여섯 workload 오류와 observer 누락을
합쳐 13개 프로세스를 직렬 실행했다. 전체 이미지 16개·153,920바이트를 확인했다.
관측된 성공 실행은 native draw/forward 각각 36개, instance 36개, vertex 108개이며
전체 이미지 hash는 별도 Python CPU 참조와 비교했다. plain 실행의 native 계수는
null이다. 기본 진단 행 12개는 성능 표본으로 승격하지 않는다.

누락 생성·누락 해제·잘못된 pipeline·누락 영역·잘못된 scissor·readback 손상·observer
누락의 7개 거부는 지정한 오류와 종료 코드 1, 빈 stdout을 요구한다. 프로세스당
120초 제한을 두고 실행 전후 binary·observer·manifest·LLGL core/backend·ICD 파일
hash를 확인한다. 원본 stdout/stderr와 receipt는 `out/linux-pso-timer-pilot/`에 있다.
이 로컬 pilot은 완전한 재생 증거 아카이브나 pinned VVL 캠페인을 대체하지 않는다.

`tests/fixtures/LinuxPsoTimer.json`은 해당 software pilot의 실제 pso8 관측 출력이다.
로컬 전체 Python 계약 1,428개가 통과했고 입력 파일 불변도 확인했다.
8개 CPU 계약 테스트는 전체 lifecycle/loop 보존, 생성 결정성·모드 구분·출력 제한,
플랫폼 anchor 변조, 이미지·기하·lifecycle·native 계수·module·시간값 변조 거부,
plain 계수 미관측을 검사한다. 이 fixture 재검사는 새 GPU 실행이 아니다.

실행 명령과 요구 환경은 `benchmarks/linux_pso_campaign/README.md`에 정리했다.

## 다음 개발

SDK/SDL과 연결한 PSO 전용 캠페인 실행기·고정 계약·통계·source/build/runtime 증거
수집·독립 재생을 추가해야 한다. required pinned VVL/core/sync 제어와 모드/UUID
거부를 측정 gate에 연결하고, observer-free 다섯 독립 프로세스 × 30회 반복을
검증해야 한다. 실제 Linux GPU positive 경로와 fresh 성능 비교, allocation 캠페인,
실제 표시 검증과 Phase 3 gate도 남아 있다.
