# Backend status

| Backend | 구현 | 컴파일 | 런타임 | 성능 비교 |
|---|---|---|---|---|
| D3D12 / Windows x64 | 초기 코어 | MSVC Release 검증 | GTX 1660 SUPER, WARP, GPU validation | LLGL/SDL3와 A/B/C 일부 |
| Vulkan / Windows x64 | 버퍼·텍스처 전송, 색상 패스, SPIR-V vertex/index draw·보간·시저·블렌딩·instance streams·2/8 outputs | MSVC Release 검증 | NVIDIA/Intel full-image·core/sync validation | 미측정 |
| Vulkan / Linux | 공통 소스 경로 있음 | 미검증 | 미검증 | 미측정 |
| Metal / macOS | 미구현 | 미검증 | 미검증 | 미측정 |

미구현 backend는 factory에서 Unsupported를 반환한다. 실제 backend처럼 보이는
가짜 객체나 CPU timer 대체 GPU 실행 결과를 제공하지 않는다.

D3D12는 DXGI high-performance adapter를 선택하며 softwareAdapter는 명시적 WARP다.
feature level 11.0 이상, 단일 direct queue, committed resource, SM5.1 DXBC를 사용한다.
validation=true이면 debug layer와 가능한 경우 GPU-based validation을 활성화한다.
debug layer가 없으면 Unsupported를 반환한다. validationErrorCount는 ERROR/CORRUPTION을 센다.
optimized clear value 미지정에 대한 성능 경고는 알려진 초기 구현 제한이다.

Vulkan은 [전송·색상 패스·vertex/index 범위](VULKAN.md)를 따른다. Depth/descriptor 바인딩 및 같은 Vulkan API
workload의 competitor 비교는 후속이다. Metal은 native Mac의 컴파일/이미지/동기화
검증 후 지원으로 표시한다. Windows 테스트 통과를 이 두 backend 검증으로 대체하지 않는다.
