# Phase 2 — 실제 창 표시·사용자 조작 수용

2026-10-26 · `P2-ACC-03` · SDK 기준 커밋 `9e44a6d`.

**사용자가 실제 모니터에서 파란 배경·움직이는 주황 사각형, 크기 변경, 최소화/복원, 최대화/복원, 가림/다시 보기와 정상 닫기를 확인했다. 실제 창 이벤트·종료 기록과 대조해 NVIDIA 기본 표시 범위의 `P2-ACC-03`을 완료했다.** 다른 adapter·VSync off·GBV·DPI 이동·예기치 않은 손실 수용으로 확대하지 않는다.

[요약](../benchmarks/reports/2026-10-26-phase2-manual-presentation/summary.json), [사용자 직접 확인](../benchmarks/reports/validation/2026-10-26-phase2-manual-presentation/human-observation.json), [최종 native 기록](../benchmarks/reports/validation/2026-10-26-phase2-manual-presentation/completed/native.jsonl), [소스 manifest](../benchmarks/reports/validation/2026-10-26-phase2-manual-presentation/source-manifest.json).

## 실행과 사람 확인

현재 설치 SDK를 소비하는 예제를 새 `out/manual-presentation`에서 빌드했다. 새 열거에서 NVIDIA GTX 1660 SUPER ID `97549`를 명시 선택했다. 이 ID는 해당 OS 세션의 관측이며 영구 ID가 아니다. Hardware·VSync on·60fps cap·validation off의 일반 loop이고, readback 및 native loss injection을 사용하지 않았다.

첫 실행 후 사용자는 “모든 조작 후 화면과 움직임이 정상이며 X로 닫았습니다”라고 답했다. 크기 변경·최대화·닫기는 실제 기록됐지만 최소화 transition은 0이어서 이 실행을 단독 수용하지 않았다. 원본과 불충분 판정을 보존하고 같은 binary로 새 세션을 열었다.

두 번째 실행은 24.824초, 1,302 frames, 실제 GPU resize 4회다. 최소화 시점의 frame 878이 복원 시점에도 878로 유지됐고, 복원 후 1,302까지 증가했다. 실제 최소화/복원 각 1회, 최대화 1회, suspended wait 8회를 기록했다. Client extent는 664×441→1123×718→619×350→1920×1009→619×350으로 바뀌었다. `wm_close` 시점과 종료의 frame 수가 같고, HWND가 살아 있는 동안 SDK objects/resources/pending submissions 0으로 해제한 뒤 native exit 0으로 끝났다. 사용자는 이어 “최소확 복원까지 확인했어 이제 다음 개발 진행해줘”라고 직접 확인했다.

두 세션 모두 같은 SDK·예제 binary·설치 library를 사용했고, 실행 중 frozen 입력은 변하지 않았다. 화면 제어 중 물리 Esc로 자동화가 중지된 사실과 별개로, 두 번째 실제 native session의 종료 원인은 `wm_close`이고 종료 코드는 0이다. 사용자 확인 없이 도구 화면만으로 완료하지 않았다.

## 관측 코드와 수용 경계

SDK 코드·공개 API·root CMake는 바꾸지 않았다. 예제의 WndProc은 최소화/복원/최대화 전이와 닫기 사유만 누적하고, normal loop가 상태 변화 시 elapsed/frame/client extent를 flush한다. GPU resize는 기존 main-loop 경로에서 처리한다. Native 기록의 `interactive_acceptance=false`는 그대로 유지한다. 메시지가 사람 또는 자동화 중 어디에서 왔는지를 Win32 메시지 자체로 단정하지 않는다.

[검증기](../tools/manual_presentation_evidence.py)는 정상 종료, source/adapter, 실제 resize·minimize/restore, 최소화 중 frame 불변, 복원 후 재개와 close 이후 추가 frame 없음을 확인한다. 별도의 직접 사용자 확인을 정확한 native JSONL SHA-256에 연결해야만 최종 수용을 true로 한다. 첫 기록의 불일치, 자동 화면 관찰만 제출한 경우, 다른 세션의 확인, 실패/timeout/변경 입력, 사용자 부정 답변은 수용하지 않는다.

```text
SDK fingerprint: 2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401
Installed/core library SHA-256: 14e09ea926bacdd412d9065199667ce2a97c37c325cb6b95ca30b6fc37993124
Accepted native SHA-256: fde5b2a89d826d84fbed999246e4f6e8a929879e5b3c276c28a41dab27ad0fcb
```

## 회귀·원본 보존

별도 presentation CTest **7/7**이 통과했다. 기존 hardware/GBV/WARP×VSync on/off 6개 자동 lifecycle과 CPU policy 검사를 유지한다. 자동 backbuffer 관측은 사람의 monitor 관측을 대신하지 않는다. 사람 확인 결합·거부 계약 6개를 포함한 전체 Python **662개**가 오류·실패·건너뜀 0으로 통과했다.

원본 두 세션·사용자 문구·실제 compiler/link 명령·회귀 로그를 보존했다. Source ZIP은 SDK 27개 및 사용한 예제·manifest·관측/검증 코드 등 own source 39개를 원래 바이트로 보존한다. 문서 안내는 실행 후 갱신했다. 외부 dependency·실행 파일·library·덤프는 ZIP에 포함하지 않았다. 이전 source ZIP·실패 기록·native false 표시는 덮어쓰지 않았다.

[원본 검증](../benchmarks/reports/validation/2026-10-26-phase2-manual-presentation/verify_evidence.py)은 보존 ZIP의 검증기를 복원해 historical source와 실제 native/human binding을 다시 검사한다.

```sh
python benchmarks/reports/validation/2026-10-26-phase2-manual-presentation/verify_evidence.py
```

`P2-ACC-04` 실제 DPI·다중 모니터와 `P2-ACC-05` 물리 손실 수용은 미완료다. `dpi_events=0`이며 실제 DPI 전환은 없었다. 장시간 메모리 `P2-ACC-01`과 Intel AV 원인 `P2-ACC-02`, Linux 물리 GPU 수용도 이번 사람 확인으로 통과시키지 않는다.
