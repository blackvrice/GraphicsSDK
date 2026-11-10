# P7-PKG-15 Windows 배포 후보의 오프스크린 GPU 검사

[P7-PKG-13의 두 Windows ZIP](PHASE7_WINDOWS_RELEASE_CANDIDATE.md)을
그대로 사용했다. SDK 버전은 `0.0.1`, 소스는
`7c5bf927ff5902e3ae975dd78c70882f2838e367`이다.
Windows 빌드 26320에서 MSVC 19.44.35228.0과 Windows SDK 10.0.26100.0으로
독립 소비자를 빌드했다.

| 실행 | 관측한 장치 | 결과 |
| --- | --- | --- |
| D3D12 WARP | Microsoft Basic Render Driver | 64 MiB 리소스 보유·반환, Depth/MRT 패스의 RGBA8 결과 1,024픽셀 검증 |
| D3D12 하드웨어 | NVIDIA GeForce GTX 1660 SUPER | 같은 64 MiB 리소스 계약과 1,024픽셀 검증 |
| Vulkan 전송 | GTX 1660 SUPER와 Intel UHD Graphics 770 | 717개 검사, 전송 48회, 57,042,546바이트 일치 |
| Vulkan 그리기 | 전송과 같은 두 어댑터 | 637개 검사, draw 62회, 이미지 66개의 249,491,416 텍셀 바이트 일치 |

정확한 버전의 `GraphicsSDK::Core`를 공백이 포함된 새 설치 경로에서
찾는다. 세 소비자의 MSBuild 추적에서 재배치한 공개 헤더 13개와 정적
라이브러리의 사용을 확인했다. 모든 실행의 SDK 지문을 고정 매니페스트와
대조했다. D3D12는 `--warp`와 `--hardware`를 별도로 실행하고 실제 어댑터
종류를 확인한다. Vulkan은 전송과 그리기에서 같은 어댑터 이름과 ID를
확인한다.

## 재현

Windows에서 두 ZIP, 원본 매니페스트, 해당 후보와 같은 MSVC·Windows SDK,
Vulkan loader·ICD와 물리 GPU를 준비한다. 저장소 루트에서 새 출력 디렉터리를
지정한다.

```powershell
$pin = '0b5d1474c9cdeb2de6b94647f032db2999f0039c701b09316c2341a39d4856b1'
python tools/windows_release_gpu.py --candidate out/phase7-windows-release --manifest-sha256 $pin --output out/phase7-windows-gpu-smoke
python -m unittest discover -s tests -p 'test_windows_release_gpu.py'
```

`receipt.json`은 원본 ZIP과 라이브러리, 복사한 소비자 입력, 실행 파일,
MSBuild 추적과 로그의 해시를 연결한다. 위 실행의 receipt SHA-256은
`2b4a28eb5c0c57029918b72f4b1f0906c46eff1e8896c55d140cb85dff1d76d2`다.
잘못된 매니페스트 해시는 소비자 빌드 전에 거부됐다.

실행 범위는 오프스크린 설치 소비자다. 네이티브 검증 계층, 창 표시,
드라이버별 지원 판정, 성능 측정은 별도 범위다. 전체 지원 구성의
CI와 릴리스 smoke 수용은 `P7-PKG-06`에 남아 있다.
