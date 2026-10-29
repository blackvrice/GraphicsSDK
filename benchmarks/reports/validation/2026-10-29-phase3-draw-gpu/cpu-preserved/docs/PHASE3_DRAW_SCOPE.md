# P3-BEN-04 CPU 비교와 GPU timestamp 비교 분리

2026-10-27 사용자에게 기존 `P3-BEN-04`의 CPU/GPU 완료 문구와 고정 BEN-01 draw 계약의
CPU-only 범위 차이를 설명하고 범위를 확인했다. 사용자는 다음을 선택했다.

> 고정 CPU 비교 계약으로 완료하고 GPU timestamp 비교는 별도 미완료 항목으로 분리

따라서 `P3-BEN-04`는 기존 65×37 RGBA8/scissor/fullscreen triangle의 draw1, draw1000,
instanced1000을 CPU record/submit/wait/total 완료 wall time으로 새 검증·측정한다.
`wait_ms`나 `total_ms`를 GPU timestamp로 바꾸지 않는다. 실제 core/sync validation과
changing full-image readback, shader/geometry/장치 귀속과 분리된 반복 process는 그대로 필요하다.

GPU timestamp 비교는 새 필수 미완료 작업 **P3-BEN-09**로 분리한다. 고정 SDL 공개 GPU
API에는 timestamp query가 노출되지 않으므로 두 SDK의 같은 GPU 구간을 확인할 별도
계측 경로가 필요하다. SDK 자체의 query 지원이나 기존 timestamp suite만으로 경쟁 비교를
완료 체크하지 않는다. native counter·queue valid bits/period·availability/fence·정확한 장치
귀속·동일 구간과 validation/계측 영향 분리가 실제로 확인돼야 한다.

기존 작업 ID는 재사용하거나 다시 번호를 붙이지 않는다. `P3-GATE-02`에 P3-BEN-09를
선행 조건으로 연결해 GPU 비교 요구가 최종 수용에서 빠지지 않게 한다. 이 범위 결정은
CPU 재측정의 성공이나 GPU 비교 완료를 미리 승인한 것은 아니다.
