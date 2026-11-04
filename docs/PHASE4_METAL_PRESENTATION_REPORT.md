# P4-MTL-06 Metal 화면 출력

macOS 앱은 `CAMetalLayer`를 창의 뷰에 연결하고 `SurfaceKind::MetalLayer`로 전달한다.
공개 `Surface`의 `window`에 `(__bridge void*)layer`를 담고 `connection`과 `windowId`는
0으로 둔다. SDK는 메인 스레드에서 생성한 Metal 장치로 이 표면을 사용하며, 한 레이어에
하나의 활성 스왑체인을 허용한다. 앱이 창의 이벤트 루프와 레이어 배치를 관리한다.

`SwapChainDesc`는 픽셀 너비·높이, drawable 수 2 또는 3, `vsync`를 받는다. 레이어는
선택한 장치와 BGRA8Unorm으로 구성하고 drawable 읽기를 허용한다. 동기화 옵션은 생성 시
정하고 픽셀 크기는 `resize()`로 변경한다. `checkSurfaceSupport`는 레이어 종류와 장치 일치 여부를 검사한다.

1. `acquire()`로 drawable을 얻는다. 현재 프레임의 반복 호출은 같은 텍스처를 반환한다.
   레이어가 drawable을 반환하지 않으면 `Timeout`이다.
2. `RenderTarget`으로 전환해 BGRA8 파이프라인으로 그린다. 읽기 검증이 필요하면 `CopySource`로
   전환해 readback 버퍼에 복사한다. `Present` 상태로 전환한 뒤 명령을 제출한다.
3. `present()`는 초기화되고 `Present` 상태로 제출된 drawable을 같은 Metal 큐에서 표시한다.
   이 호출이 성공하면 이전 drawable을 대상으로 한 새 기록과 기존 목록의 제출이 거부된다.
   렌더 펜스는 앞선 렌더 명령의 완료를, `waitIdle()`은 표시 명령까지 포함한 큐의 완료를 확인한다.
   화면에 표시된 시점은 별도의 Metal presented callback에서 관찰한다.
4. 창의 픽셀 크기가 바뀌면 현재 `TexturePtr`를 해제하고 `resize()`를 호출한다. 큐 완료 후
   이전 drawable을 폐기하고 픽셀 크기를 갱신한다. 최소화나 0 크기일 때는 앱이 프레임을 일시 중지한다.
   창을 닫을 때 스왑체인을 먼저 해제한다. 장치가 먼저 퇴역하는 경우에도 레이어 점유가 해제된다.

drawable을 참조하는 목록은 제출 시점까지 CPU 기록을 보관한다. 표시나 크기 변경으로 이미 만료된
drawable을 네이티브 명령 버퍼가 붙잡는 일을 막고, 유효한 제출만 큐 완료까지 소유한다.
[CAMetalLayer](https://developer.apple.com/documentation/quartzcore/cametallayer),
[nextDrawable](https://developer.apple.com/documentation/quartzcore/cametallayer/nextdrawable%28%29)

## macOS에서 실행

macOS 데스크톱 세션과 C++23을 지원하는 Xcode 도구로 아래 명령을 실행한다.

```sh
cmake -S . -B out/metal-wsi-core -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGFX_ENABLE_METAL=ON -DGFX_ENABLE_D3D12=OFF -DGFX_ENABLE_VULKAN=OFF \
  -DGFX_BUILD_TESTS=OFF -DGFX_BUILD_EXAMPLES=OFF
cmake --build out/metal-wsi-core
cmake --install out/metal-wsi-core --prefix "$PWD/out/metal-wsi-install"
cmake -S tests/macos_metal_presentation -B out/metal-wsi-test -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/out/metal-wsi-install"
cmake --build out/metal-wsi-test
out/metal-wsi-test/metal_presentation --contracts
out/metal-wsi-test/metal_presentation --window
out/metal-wsi-test/metal_presentation --interactive
```

`--contracts`는 레이어 유형, 메인 스레드, 중복 점유, drawable 부재, 픽셀 크기 변경과 장치 퇴역을
검사한다. `--window`는 AppKit 창에 크기 변경 전후 각각 네 프레임을 그려 전체 BGRA 픽셀을
참조와 비교한다. 양쪽에서 양수 표시 타임스탬프를 요구한다. CI의 `--runner-window`는
타임스탬프가 누락되면 같은 레이어에서 SDK를 거치지 않는 Metal clear와 표시를 대조 실행한다.
직접 Metal 경로에서도 양수 타임스탬프가 없으면 readback·명령 완료와 화면 관찰을 구분해 기록한다.
`--interactive`에서는 움직이는 경계를 보며 크기 변경, 최소화·복원 및 닫기 동작을 직접 확인할 수 있다.

## 2026-11-04 실행 결과

소스 `5a0427b976aca7f08c3d3299d5cc8e76dba79884`에서 두 macOS 아키텍처가 같은 SDK 지문
`7f89bd2d3460289aa5d65b1786cdccbd30585caf4e442b0c294ab24e7fae6a06`으로 실행됐다.
창 소비자는 각 runner에서 새로 설치한 SDK의 헤더와 정적 라이브러리에 연결했다.
[macOS CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41779571408)

| 검사 | ARM64 | Intel x86_64 |
|---|---:|---:|
| Metal ON CTest | 15/15 | 15/15 |
| Python 계약 | 1,501/1,501 | 1,501/1,501 |
| 설치 SDK 레이어 수명 검사 | 통과 | 통과 |
| BGRA 전체 픽셀 비교 프레임 | 8/8 | 8/8 |
| 비교한 BGRA 픽셀 바이트 | 106,688 | 106,688 |
| 양수 표시 콜백, 크기 변경 전 → 후 | 0 → 0 | 1 → 2 |
| 직접 Metal 대조 프레임의 양수 콜백 | 0/3 | 해당 없음 |

각 실행은 65×31과 99×47의 drawable에서 각각 네 프레임을 비교했다. 표시 또는 크기 변경
전에 만든 기록이 이후 제출에서 거부되는 것도 검사했다. ARM에서는 SDK 표시 명령과 직접 Metal
대조 명령이 완료됐고, 화면 표시 시각 관찰은 Intel에서 이루어졌다. 실제 Mac GPU와 사람이 직접
조작하는 창 검증은 남아 있다. [원본 JSON·빌드 로그와 해시 목록](../benchmarks/reports/validation/2026-11-04-metal-presentation/summary.json)

Windows와 Linux 회귀 검사 및 Linux Vulkan 컴파일을 통과했다.
[Windows·Linux CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41779571377),
[Linux Vulkan 컴파일](https://github.com/blackvrice/GraphicsSDK/actions/runs/41779571334)
