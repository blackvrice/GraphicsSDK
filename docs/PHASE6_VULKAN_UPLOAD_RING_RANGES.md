# Vulkan Upload ring 범위 모델

`P6-OPT-10`은 [기준선](PHASE6_VULKAN_UPLOAD_STAGING_BASELINE.md)의 staging
후보가 지켜야 할 바이트 범위와 소유권 규칙을 독립 C++ 실험으로 검증한다.

## 범위와 소유권

- 하나의 장치 세대와 큐에 대해 링 하나를 두고 호출을 직렬화한다. 용량은
  2의 거듭제곱인 정렬 단위의 배수다. 예약은 정렬된 연속 범위를 돌려준다.
  링의 끝에 공간이 부족하면 앞쪽으로 순환하고, 살아 있는 범위와 겹치면
  포화를 반환한다.
- 예약한 범위는 쓰기가 가능하다. `seal` 이후에는 기록이 범위를 보유하고
  덮어쓰기를 막는다. 기록을 폐기한 호출자는 `cancel`로 범위를 반환할 수 있다.
- 호출자는 `canSubmit`으로 제출할 티켓 전체와 fence를 확인한다. native 제출이
  성공하면 `submit`으로 소유권을 fence에 넘긴다. 실패하면 기록이 범위를 계속
  보유하므로 재시도하거나 기록을 폐기할 수 있다.
- `complete`에는 같은 큐에서 확인한 누적 완료 값을 전달한다. 취소됐거나
  완료된 범위를 예약 순서의 맨 앞부터 반환한다. 앞선 미제출 범위가 살아
  있으면 그 뒤의 완료 범위도 보류한다. 티켓은 장치 세대와 예약 일련번호를
  포함한다. 호출자는 native 자원과 사용자를 정리한 뒤 `retire`를 호출한다.

## 검증

경계 테스트는 정확히 맞는 예약, wrap과 포화, 64비트 정렬, 미제출 보유,
배치 제출 거부, 제출 실패 후 재시도, fence 완료 순서, 장치 세대 교체를
다룬다. 별도의 절대 위치 모델과 비교하는 고정 시드 테스트는 여섯 용량·정렬
조합에서 총 120,000개 연산을 수행하며 겹침, 쓰기 권한과 반환 순서를 확인한다.

| 환경 | CTest | 조건 검사 | 스트레스 연산 |
| --- | --- | ---: | ---: |
| Windows x64 · MSVC 19.44 · Release | 1/1 통과 | 461,773 | 120,000 |
| Linux Docker · GCC 14.2 · Release | 1/1 통과 | 461,773 | 120,000 |

저장소 루트에서 실행한다.

```powershell
cmake -S experiments/vulkan_upload_ring -B out/upload-ring-win -G "Visual Studio 17 2022" -A x64
cmake --build out/upload-ring-win --config Release
ctest --test-dir out/upload-ring-win -C Release --output-on-failure -V
```

```bash
cmake -S experiments/vulkan_upload_ring -B out/upload-ring-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14
cmake --build out/upload-ring-linux
ctest --test-dir out/upload-ring-linux --output-on-failure -V
```

실제 Vulkan backing의 map·flush, 복사 오프셋, native 제출과 GPU readback은
`P6-OPT-08` 범위이며 Shader/PSO 수용(`P5-SHD-08`) 이후 진행한다.
성능과 채택 여부는 같은 업로드 기준선으로 `P6-OPT-09`에서 평가한다.
