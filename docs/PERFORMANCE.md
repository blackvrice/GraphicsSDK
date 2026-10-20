# Performance policy

수치 목표와 SDK 전체 순위는 실제로 비교 가능한 반복 측정 이후에만 결정한다.
현재 수치는 제한된 D3D12 headless workload의 탐색 결과다. 실제 UI/게임/영상 성능을
대표하지 않는다. correctness, resource lifetime, validation을 희생해 점수를 높이지 않는다.

CPU record, submit 및 fence wait의 wall time은 구분한다. timestamp는 GPU query에서만
얻는다. 공통 SDL3 GPU API에 timestamp query가 없으므로 현재 세 SDK 공통 비교는
추가 query 명령 없이 실행하고 gpu_ms=null을 저장한다. GraphicsSDK의 별도 correctness
테스트는 실제 GPU timestamp도 검증하지만 그 한 번의 시간을 경쟁 baseline으로 쓰지 않는다.

LLGL End와 SDL Submit은 내부 작업 경계가 다르다. record/submit 단일 구간으로
우위를 주장하지 않고 전체 wall time과 SDK 정책을 함께 본다. 리소스 해제 API와
실제 회수 대기를 더해 비교한다. 업로드는 미리 준비한 CPU payload에서 GPU 완료까지
측정하며 SDK 내부 추가 memcpy/할당은 포함된다. shader compile과 readback 검증은 밖이다.

작업을 최적화하기 전에 같은 suite의 baseline을 보존한다. metadata와 소스/build
fingerprint가 다른 원자료를 덮어쓰지 않는다. 차이는 분산·회차별 결과와 함께 보고하고
측정된 원인이 없는 임의 비율 개선 목표를 만들지 않는다.
문서: [BENCHMARK_DESIGN](BENCHMARK_DESIGN.md), [BENCHMARK_REPORT](BENCHMARK_REPORT.md),
[OPTIMIZATION_LOG](OPTIMIZATION_LOG.md).
