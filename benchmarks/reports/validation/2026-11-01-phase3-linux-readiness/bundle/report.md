# Linux 세 SDK software correctness 통합

Native source: `7f684d898595530b54d04242f31f98979c146245`. 기존 원본의 재검사이며 성능 측정·물리 GPU 수용이 아니다.

13개 입력 × 3 SDK × 일반/필수 validation = 78개 정상 실행. workload·제어 124개 프로세스에는 core/sync 제어 8개·거부 제어 38개를 포함한다. 선행 qualification/probe/smoke는 이 합계에 다시 포함하지 않는다.

| 구성 | 정상 | core/sync | 거부 | 이미지 | 버퍼 | bytes |
|---|---:|---:|---:|---:|---:|---:|
| upload | 8 | 2 | 5 | 32 | 0 | 862694560 |
| draw | 6 | 2 | 9 | 24 | 0 | 230880 |
| pso | 4 | 2 | 11 | 16 | 0 | 153920 |
| allocation | 8 | 2 | 13 | 8008 | 8008 | 109837728 |
| matrix | 52 | 0 | 0 | 16160 | 16016 | 1945834176 |

전체 이미지 24,240개·버퍼 24,024개·2,918,751,264 bytes.

개별 validator를 먼저 재생하고 동일 qualification·LLGL build·source revision·공통 source/runtime hash와 전역 직렬 순서를 대조했다.
SDK/SDL의 156개 CPU 진단 행은 원본에만 보존한다. 통계·순위·성능 표본은 생성하지 않는다.
SDK/SDL 장치 귀속과 LLGL 실제 native handle·draw 관측의 차이를 coverage에 명시한다. 서로 다른 SDK의 payload hash를 직접 같다고 주장하지 않는다.

LLGL의 실제 Linux GPU 모드와 세 SDK Linux 성능 캠페인은 별도 구현·환경 검증이 남아 있다. handoff.json은 미실행 준비 목록이며 워크플로를 자동 실행하지 않는다.
실제 Linux GPU·사람의 표시 조작·최종 gate와 Phase 3는 미완료다. source ZIP 또는 native code를 실행하지 않는다.
