# Backend status

| Backend | 구현 | 컴파일 | 런타임 | 성능 비교 |
|---|---|---|---|---|
| D3D12 / Windows x64 | 초기 코어 | MSVC Release 검증 | GTX 1660 SUPER, WARP, GPU validation | LLGL/SDL3와 A/B/C 일부 |
| Vulkan / Windows x64 | 버퍼·색상/D32 전송, 색상·깊이 패스, SPIR-V vertex/index draw·보간·시저·블렌딩·instance streams·2/8 outputs, 텍스처/상수 배열·정적 sampler·GPU timestamps·Win32 출력(maintenance1 지원 장치) | MSVC Release 검증 | NVIDIA/Intel full-image·core/sync validation | 미측정 |
| Vulkan / Linux | 공통 소스 경로 있음 | 미검증 | 미검증 | 미측정 |
| Metal / macOS | 미구현 | 미검증 | 미검증 | 미측정 |

미구현 backend는 factory에서 Unsupported를 반환한다. 실제 backend처럼 보이는
가짜 객체나 CPU timer 대체 GPU 실행 결과를 제공하지 않는다.

D3D12는 DXGI high-performance adapter를 선택하며 softwareAdapter는 명시적 WARP다.
feature level 11.0 이상, 단일 direct queue, committed resource, SM5.1 DXBC를 사용한다.
validation=true이면 debug layer와 가능한 경우 GPU-based validation을 활성화한다.
debug layer가 없으면 Unsupported를 반환한다. validationErrorCount는 ERROR/CORRUPTION을 센다.
optimized clear value 미지정에 대한 성능 경고는 알려진 초기 구현 제한이다.

Vulkan은 [전송·색상/깊이 패스·vertex/index·바인딩·timestamp 범위](VULKAN.md)를 따른다. Presentation 및 같은 Vulkan API
workload의 competitor 비교는 후속이다. Metal은 native Mac의 컴파일/이미지/동기화
검증 후 지원으로 표시한다. Windows 테스트 통과를 이 두 backend 검증으로 대체하지 않는다.

Vulkan Win32 출력은 자원 완료 펜스가 있는 장치만 지원한다. 현재 NVIDIA는 실제 출력·전체
backbuffer 검증을 통과했고 Intel 드라이버는 해당 feature가 없어 Unsupported를 반환한다.
기존 offscreen suite는 두 장치에서 실행한다. Linux WSI·사람의 화면 관찰·성능 수용은 별도다.
[세부 계약과 증거](PHASE3_VULKAN_PRESENTATION_REPORT.md)를 따른다.
