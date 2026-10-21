# Performance policy

수치 목표와 SDK 전체 순위는 실제로 비교 가능한 반복 측정 이후에만 결정한다.
현재 수치는 제한된 D3D12 headless workload의 탐색 결과다. 실제 UI/게임/영상 성능을
대표하지 않는다. correctness, resource lifetime, validation을 희생해 점수를 높이지 않는다.

CPU record, submit 및 fence wait의 wall time은 구분한다. timestamp는 GPU query에서만
얻는다. SDL3를 포함한 기존 end_to_end 비교는 추가 query 없이 gpu_ms=null을 유지한다.
별도 draw_gpu/instanced_gpu는 GraphicsSDK/LLGL/Forge의 동일한 GPU marker 구간을
비교한다. Diligent duration query의 시작 resolve 비용은 구간 안에 있어 별도 계약으로
기록하고 직접 비교를 거부한다. [GPU timestamp 보고서](GPU_TIMESTAMP_BENCHMARK_REPORT.md)에
실제 반복 결과와 범위가 있다. correctness 테스트의 한 번의 시간을 baseline으로 쓰지 않는다.

LLGL End와 SDL Submit은 내부 작업 경계가 다르다. record/submit 단일 구간으로
우위를 주장하지 않고 전체 wall time과 SDK 정책을 함께 본다. 리소스 해제 API와
실제 회수 대기를 더해 비교한다. 업로드는 미리 준비한 CPU payload에서 GPU 완료까지
측정하며 SDK 내부 추가 memcpy/할당은 포함된다. shader compile과 readback 검증은 밖이다.

작업을 최적화하기 전에 같은 suite의 baseline을 보존한다. metadata와 소스/build
fingerprint가 다른 원자료를 덮어쓰지 않는다. 차이는 분산·회차별 결과와 함께 보고하고
측정된 원인이 없는 임의 비율 개선 목표를 만들지 않는다.
문서: [BENCHMARK_DESIGN](BENCHMARK_DESIGN.md), [BENCHMARK_REPORT](BENCHMARK_REPORT.md),
[OPTIMIZATION_LOG](OPTIMIZATION_LOG.md).

CPU 사용률은 선택형 telemetry에서 측정 batch의 프로세스 CPU/wall 비율로 기록한다.
논리 CPU 하나=100%이며 SDK 내부 worker도 포함한다. CPU lifetime memory peak와 GPU
checkpoint 최대 사용량은 서로 다른 범위다. GPU 값은 연속 peak가 아니고 budget도 소비량이
아니다. 관측 비용이 들어간 결과는 비계측 원본과 자동 비교하지 않는다.
[CPU·메모리 보고서](CPU_MEMORY_BENCHMARK_REPORT.md)에 원본·표본 수·해석 제한을 기록했다.
고정 LLGL의 full-region WriteTexture after readback 오류는 public buffer-to-texture
adapter 경로로 복구했다. 추가 staging copy·대기 비용을 포함한 [새 측정](LLGL_UPLOAD_BENCHMARK_REPORT.md)만
현재 correctness 근거로 사용하고 과거 오류 경로 수치는 재사용하지 않는다.
