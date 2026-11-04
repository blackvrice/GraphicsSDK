# P5-SHD-05 SPIR-V reflection과 Vulkan 바인딩 검사

`reflectSpirvInterface`는 외부에서 검증한 Vulkan 1.1용 SPIR-V 1.3 모듈에서 정점 또는
픽셀 진입점 하나를 읽는다. 선언한 단계가 실행 모델과 일치해야 한다. 입출력은
사용자 위치와 float32 성분 수를 오름차순으로 반환한다. Position, VertexIndex 같은
내장 값은 사용자 위치 목록에서 제외한다.

리소스는 descriptor set과 binding 순으로 정렬한다. `ShaderBinding::space`는
`DescriptorSet`, `slot`은 `Binding`이다. `count`는 고정 배열의 길이이고
`minimumBytes`는 연속된 `float4` Uniform Block 한 요소의 최소 바이트 수다.
이름은 선택적인 `OpName`에서 읽으며 디버그 이름이 생략된 모듈은 빈 문자열을 반환한다.
`reflectShader`로 같은 리소스 목록만 조회할 수 있다.
[Khronos SPIR-V 명세](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html)

```cpp
auto reflection = gfx::reflectSpirvInterface(spirvShader);
if (!reflection) {
    return std::unexpected(reflection.error());
}
for (const auto& resource : reflection->bindings) {
    // resource.space, resource.slot, resource.count
}
```

Vulkan 파이프라인은 같은 파서의 결과로 `main`과 set 0, descriptor 종류·개수·단계 가시성,
정점 레이아웃, 단계 사이의 보간 값과 색상 출력 위치를 검사한다. 별도 sampled Texture2D,
정적 sampler와 최대 64KiB의 연속 `float4` Uniform Block이 지원 프로필이다. 공개 reflection은
모듈의 실제 set과 진입점 이름을 반환하며, 파이프라인의 현재 제약은 생성 단계에서 적용한다.

SDK는 명령 프레이밍과 지원 인터페이스를 검사한다. 전체 의미 검증에는 입력을 Vulkan에
전달하기 전에 `spirv-val --target-env vulkan1.1`을 사용한다.
[DXC 컴파일 및 외부 검증](PHASE5_VULKAN_SPIRV_REPORT.md)

## 재현

```sh
ctest --test-dir out/linux-vulkan -R '^spirv_.*_contracts$' --output-on-failure
ctest --test-dir out/linux-vulkan -R '^vulkan_(resource_bindings|vertex_input)$' --output-on-failure
```

CPU 검사는 선언 메타데이터, 이름 없는 모듈, 비기본 진입점·descriptor set, 중복 binding,
종결되지 않은 이름과 단계 불일치를 다룬다. Vulkan 실행은 설치 SDK 소비자에서 실제 descriptor 배열과
Uniform Block을 사용하고 잘못된 종류·개수·가시성·sampler 슬롯을 거부하는지 확인한다.

## 검증 결과

소스 커밋 `adce0705a06e981d0aa8b17377fa524eb4d0ea1b`에서 확인했다.

- 로컬 Windows에서 SPIR-V CPU 계약 3개와 D3D12 바인딩 레이아웃 계약을 통과했다.
  SPIR-V 바인딩 테스트는 73개 검사를 실행했다.
- Linux x64 lavapipe의 소스 CTest와 설치 SDK 소비자는 각각 바인딩 검사 291개,
  draw 78개, descriptor snapshot 129개, 이미지 40개를 확인했다. 정점 검사도 각 실행에서
  이미지 73개를 확인했다. [Vulkan 입력·바인딩 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41852811329)
- 고정 DXC가 생성하고 외부 검증한 SPIR-V에서도 공개 인터페이스를 조회했다.
  `pixelMain`은 reflection으로 읽히며, 현재 Vulkan 파이프라인은 이 진입점을 거부한다.
  정상 정점·픽셀 모듈의 렌더링은 기존 D3D12 이미지와 일치했다.
  [DXC → Vulkan CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41852811126)
