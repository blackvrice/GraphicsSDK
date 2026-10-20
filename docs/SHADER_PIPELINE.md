# Shader pipeline

현재 동작하는 bootstrap 경로는 Windows `D3DCompile` → SM5.1 DXBC → D3D12다.
entry point와 stage를 명시하고 최적화 level 3 및 warnings-as-errors를 사용한다.
잘못된 소스는 compiler 진단과 HRESULT를 Result에 담는다. include 파일 해석은 현재
제공하지 않는다. `D3DReflect`로 이름/register/count/space를 읽을 수 있다.

목표 경로인 DXC → DXIL/SPIR-V → SPIRV-Cross → MSL은 아직 구현하지 않았다.
DXC 설치 유무와 이 경로의 동작 여부는 별도이며, 현재 bootstrap을 Phase 5 완료로
표시하지 않는다. compile cache 및 pipeline cache도 아직 없다.

초기 pipeline은 vertex-ID 입력과 선택적인 fragment texture t0/space0 및 sampler
s0/space0만 사용한다. 임의의 reflection 결과에서 범용 layout을 자동 생성하지 않는다.
texture binding은 명령 목록별 256개 shader-visible descriptor를 순차 사용하여
이미 기록한 draw의 descriptor를 덮어쓰지 않는다. 한도 초과는 실패한다.

공통 draw 벤치마크는 같은 source를 한 번 compile하여 동일 bytecode를 세 SDK에
전달한다. compile 시간은 draw 측정 밖이다. 결과에 bytecode FNV-1a hash를 기록한다.
이 hash는 재현성 식별자이며 보안용 digest가 아니다.

추후 compile cache key에는 source/include 전체 content, compiler version,
entry/stage/profile/flags/defines와 target을 포함한다. disk cache의 cold/warm 상태와
driver PSO cache 상태를 구분해 D 벤치마크에서 검증해야 한다.
