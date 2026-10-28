# LLGL 명시적 패치 비교 프로필 범위 결정

2026-10-28, P3-CMP-03 실제 검증 결과와 10줄 패치를 제시한 뒤 사용자에게 다음 선택을 요청했다.

> 고정 LLGL 원본은 네 업로드 입력 모두 sync validation에 실패했고, 별도 ‘고정 커밋+10줄 패치’ 후보는 32개 이미지 전체 검증을 통과했습니다. 향후 LLGL 비교 입력을 어떻게 정할까요?

사용자 답변:

> 명시적 패치 프로필을 허용해 계속 진행

이에 따라 향후 추가 LLGL 비교에 **`LLGL 649c0cc + transfer-src patch v1`** 프로필을 허용한다. 원본 고정 목록은 바꾸지 않으며, 고정 커밋 `649c0cc8fa608312c5e4b94dae409a5859fc7553`에 `VKCommandContext.cpp`의 transfer-source read access/stage 처리 10줄만 추가한 것으로 한정한다. patch·원본/수정 파일·실제 compiler/linker input과 runtime artifact 해시를 함께 기록해야 한다.

이 프로필을 **수정되지 않은 LLGL 원본**으로 표시하지 않는다. 원본은 네 업로드 입력의 sync 오류가 보존되며 correctness/성능 수용 대상이 아니다. 수정 프로필의 업로드 correctness만 이번에 완료한다. draw/instancing·PSO·allocation correctness, fresh 세 SDK 성능 비교와 다른 필수 gate는 별도 미완료로 유지한다. CPU 시간을 GPU timestamp로 대체하지 않는다.

실험 당시 기록은 `transfer-src-candidate`로 식별되며 `adopted_as_comparison_baseline=false`다. 이는 사용자 결정 **전** 검증 이력이다. 이 원본을 고쳐 과거에 채택된 것처럼 만들지 않는다. 이후 채택 상태는 [비교 프로필 계약](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-profile.json)으로 별도 연결한다. 이 결정은 validation 통제, 전체 readback, 작업 크기/반복, 라이선스 감사, raw 표본 보존 요구를 줄이지 않는다.
