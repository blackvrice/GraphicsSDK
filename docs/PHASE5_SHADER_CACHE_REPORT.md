# P5-SHD-06 오프라인 DXC 진단과 캐시

Windows x64 DXIL과 Linux x64 Vulkan 1.1용 SPIR-V 1.3 명령에 `--cache-dir`을
선택적으로 지정한다. 각 실행의 출력 디렉터리는 새로 만들고 캐시 디렉터리를 재사용한다.
성공 시 생성된 모듈과 `compile-receipt.json`의 상태, 키, 모듈 SHA-256을 확인할 수 있다.

```powershell
python tools/d3d12_dxil.py compile --source tests/fixtures/shader_toolchain/triangle.hlsl --entry pixelMain --stage frag --output out/dxil-cold --cache-dir out/dxc-cache
python tools/d3d12_dxil.py compile --source tests/fixtures/shader_toolchain/triangle.hlsl --entry pixelMain --stage frag --output out/dxil-warm --cache-dir out/dxc-cache
```

```sh
python3 tools/vulkan_spirv.py compile --source tests/fixtures/shader_toolchain/triangle.hlsl \
  --entry pixelMain --stage frag --output out/spv-cold --cache-dir out/dxc-cache
```

Linux에서도 출력 디렉터리만 바꿔 같은 명령을 실행하면 캐시 적중을 확인할 수 있다.
캐시는 두 오프라인 명령의 DXC 셰이더 객체에 적용된다. SDK의 인프로세스 HLSL 컴파일,
SPIRV-Cross 변환과 네이티브 파이프라인 캐시는 각각 별도 경로다.

## 키와 검증

- 키는 캐시 형식 버전, 원본 소스 SHA-256, DXC 전처리 결과 SHA-256, 고정 도구 식별자,
  진입점·단계별 프로필을 포함한 인자와 대상을 묶는다. 전처리 결과에 포함되는 중첩 include
  변경과 매크로 확장이 키에 반영된다. 위치를 보존하는 `#line`도 전처리 결과에 포함된다.
- 캐시 미스에서는 전처리 결과를 컴파일한다. 적중 시 DXC 코드 생성은 건너뛰고,
  include 변경을 감지하기 위한 전처리와 대상 검증을 수행한다. DXIL은 `dxv`, SPIR-V는
  `spirv-val` 및 단계·진입점 검사를 거친 뒤 출력한다.
- 레코드의 키와 객체 해시가 일치해야 적중으로 처리한다. 잘리거나 변경된 레코드는
  재컴파일한 뒤 같은 디렉터리의 임시 파일을 원자적으로 교체한다. 캐시 객체 한도는 4MiB다. 상태는
  `miss`, `hit`, `repaired`로 기록하며, 캐시 옵션을 생략하면 `disabled`다.
- 컴파일러 오류는 대상과 프로필, 원본 HLSL 파일·행·열 및 도구 진단을 표시한다.
  실패한 셰이더는 출력이나 새 캐시 항목으로 공개되지 않는다.

도구의 고정 버전과 라이선스는 [오프라인 도구 계약](PHASE5_SHADER_TOOLCHAIN_REPORT.md)에 있다.

## 검증 결과

소스 커밋 `7395b266e76401017ce02c535c92ab641b3750a8`의 Windows D3D12 WARP와 Linux
Vulkan lavapipe에서 다음 결과를 확인했다.

- 첫 요청은 미스였다. 같은 입력의 두 번째 요청은 전처리와 대상 검증을 수행하고 DXC
  코드 생성을 건너뛰었다. 두 결과의 객체 SHA-256은 일치했다.
- 잘린 레코드는 원래 객체와 같은 바이트로 복구됐다. 중첩 include의 색상 값을 바꾸면
  새로운 키와 객체가 생성됐다.
- 실패한 소스는 `cache-fixture.hlsl:2:42`, 대상 프로필과 `missingSymbol`을 진단했다.
- 캐시 적중에서 얻은 픽셀 셰이더를 설치 SDK에서 실행해 두 렌더러의 13×7 RGBA8
  전체 364바이트가 기존 참조와 일치했다.

[D3D12 WARP CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42544879418) ·
[Vulkan lavapipe CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42544879419)
