# Docker·GitHub CLI로 다른 OS 검사하기

2026-10-29 기준. Windows 개발 PC에서 **로컬 Linux 검사는 Docker**, **원격 Windows/Linux CI는 GitHub CLI**를 사용한다. macOS는 별도 GitHub runner workflow를 추가하는 예시를 아래에 둔다. 이 문서를 추가한 것만으로 OS/GPU 수용 항목을 완료 처리하지 않는다.

## 1. 검사 범위 선택

| 목적 | 실행 방법 | 확인하는 범위 | 별도 확인이 필요한 것 |
|---|---|---|---|
| Linux CPU·공개 계약 | Docker `core` | 실제 Linux compiler와 CPU CTest | Vulkan 실행·물리 GPU |
| Linux Vulkan 빌드 | Docker `compile` 또는 `linux-vulkan-compile.yml` | Vulkan을 활성화하고 **required validation 설정을 달리한 두 preset** 빌드와 CPU 계약 | validation preset 빌드 성공만으로 layer 실행 성공을 뜻하지 않음 |
| Linux Vulkan 기능·core/sync validation | Docker `validation` 또는 `linux-vulkan-validation.yml` | Mesa software ICD의 전송·렌더링·필수 layer·오류 통제 | 물리 GPU 성능·driver 수용 |
| Linux X11/XCB | 기존 XCB CI | Xvfb·software ICD에서 창/표면·resize·수명·전체 참조 | 실제 화면을 사람이 확인하는 수용 |
| Windows 회귀 | 기존 `build.yml` | Windows SDK 빌드·기본 테스트와 Python 검사 | hosted runner의 WARP/CPU 결과를 실제 GPU/GBV 수용으로 확대하지 않음 |
| macOS CPU 이식성 | 아래 macOS workflow 추가 예시 | macOS compiler·CPU 계약 | 현재 Metal backend는 미구현이며 Metal/GPU 지원을 확인하는 검사가 아님 |
| 실제 Linux GPU | 등록된 Linux GPU self-hosted runner | 지정 ICD·장치 UUID의 실제 suite/validation | runner·driver·장치를 따로 준비해야 함 |

Docker Linux 컨테이너는 macOS를 대신 실행하는 환경이 아니다. Windows Docker Desktop에서는 Linux backend를 통해 Linux 컨테이너를 실행한다. 실제 OS 정보는 컨테이너의 `uname`과 검사 receipt에 남긴다. [Docker WSL backend 설명](https://docs.docker.com/desktop/features/wsl/).

## 2. 설치·연결 확인

새 터미널에서 저장소 root로 이동한다. 설치 직후 기존 터미널의 PATH가 갱신되지 않았다면 아래처럼 실행 파일 경로를 사용할 수 있다.

```powershell
Set-Location D:\source\GraphicsSDK

$Docker = (Get-Command docker -ErrorAction SilentlyContinue).Source
if (-not $Docker) {
    $Docker = Join-Path $env:LOCALAPPDATA 'Programs\DockerDesktop\resources\bin\docker.exe'
}
$Gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
if (-not $Gh) {
    $Gh = Join-Path $env:ProgramFiles 'GitHub CLI\gh.exe'
}
if (-not (Test-Path -LiteralPath $Docker)) { throw 'Docker 설치 경로를 지정하세요.' }
if (-not (Test-Path -LiteralPath $Gh)) { throw 'GitHub CLI 설치 경로를 지정하세요.' }

# Docker가 이름으로 실행하는 credential helper도 찾을 수 있도록 현재 터미널에만 반영한다.
foreach ($ToolBin in @([IO.Path]::GetDirectoryName($Docker), [IO.Path]::GetDirectoryName($Gh))) {
    if (($env:PATH -split ';') -notcontains $ToolBin) { $env:PATH = "$ToolBin;$env:PATH" }
}

& $Docker --version
& $Docker context show
& $Docker info --format '{{.OSType}}'
if ($LASTEXITCODE -ne 0) { throw 'Docker Desktop과 Linux 엔진 연결을 확인하세요.' }
& $Gh --version
& $Gh auth status
```

이 PC에서는 Docker `29.9.2`, context `desktop-linux`, server OS `linux`, GitHub CLI `2.103.3`과 로그인 정상 응답을 확인했다. 설치 경로가 다른 PC에서는 `$Docker`·`$Gh`를 실제 위치로 바꾼다. CLI 버전 출력만으로 Docker server 연결까지 성공했다고 판단하지 않는다.

문서 작성 중 PowerShell 구문, Dockerfile `build --check`, Bash `bash -n`, 기본 Ubuntu 컨테이너의 `uname -s`가 통과한 것을 확인했다. 아래 SDK 검사 3개 모드와 macOS 예시 전체를 이번 문서 작업에서 새로 실행한 것은 아니다.

로그인이 필요한 경우 다음은 **사용자가 브라우저에서 완료하는** 절차다. 토큰을 문서·로그·Docker 이미지에 복사하지 않는다.

```powershell
& $Gh auth login --hostname github.com --git-protocol https --web
& $Gh auth status
```

## 3. 먼저 GitHub CLI로 기존 Linux CI 실행하기

GitHub가 실행하는 것은 **원격 저장소의 지정 branch/tag**다. 로컬 미커밋·미푸시 변경은 들어가지 않는다. 확인할 변경만 선택해 커밋·푸시한 다음 아래를 실행한다.

```powershell
$Repository = 'blackvrice/GraphicsSDK'
$TargetRef = 'main'
$ExpectedSha = (& $Gh api "repos/$Repository/commits/$TargetRef" --jq '.sha').Trim()
if ($LASTEXITCODE -ne 0 -or -not $ExpectedSha) { throw '원격 revision 조회 실패' }

& $Gh workflow list --repo $Repository
& $Gh workflow run linux-vulkan-compile.yml --repo $Repository --ref $TargetRef
if ($LASTEXITCODE -ne 0) { throw 'CI 실행 요청 실패' }

# 실행 생성에 시간이 걸릴 수 있다. URL에 표시된 실행 ID 또는 아래 목록을 확인한다.
& $Gh run list --repo $Repository --workflow linux-vulkan-compile.yml `
    --branch $TargetRef --event workflow_dispatch --commit $ExpectedSha --limit 5 `
    --json databaseId,headSha,createdAt,status,conclusion,url

$RunId = Read-Host '방금 요청한 실행의 databaseId'
$RunSha = (& $Gh run view $RunId --repo $Repository --json headSha --jq '.headSha').Trim()
if ($LASTEXITCODE -ne 0 -or $RunSha -ne $ExpectedSha) { throw '다른 commit의 실행입니다.' }

& $Gh run watch $RunId --repo $Repository --exit-status --interval 10
$CiExitCode = $LASTEXITCODE
$ArtifactDir = Join-Path (Get-Location).Path ("out\ci-download-$RunId-" + [guid]::NewGuid().ToString('N'))
& $Gh run download $RunId --repo $Repository --dir $ArtifactDir
$ArtifactExitCode = $LASTEXITCODE
if ($CiExitCode -ne 0) {
    & $Gh run view $RunId --repo $Repository --log-failed
    throw 'CI 결과 또는 감시 오류: 실행 상태·내려받은 원본·실패 로그를 확인하세요.'
}
if ($ArtifactExitCode -ne 0) { throw 'Artifact 다운로드 실패: 실행 ID와 보관 기간을 확인하세요.' }
```

목록의 첫 항목을 무조건 선택하지 않는다. 동시에 실행된 다른 요청이나 과거 성공 결과를 고르지 않도록 **실행 ID·head SHA·시작 시각**을 대조한다. branch가 실행 요청 사이에 바뀌어 SHA가 다르면 새 revision을 확인하고 다시 요청한다. Artifact 다운로드 실패는 별도로 종료 코드를 확인한다. 저장소의 일반 artifact 보관 기간은 각 workflow에서 확인한다. 만료된 artifact는 성공 로그만으로 재구성하지 않는다.

`gh workflow run`은 `workflow_dispatch`가 있는 workflow만 실행할 수 있다. 현재 `build.yml`은 push/PR 전용이므로 `gh workflow run build.yml`을 사용하지 않는다. 실제 변경을 푸시해 생성된 실행을 `gh run list --workflow build.yml --commit <SHA>`로 찾는다. [수동 실행 명령](https://cli.github.com/manual/gh_workflow_run), [종료 코드와 함께 기다리기](https://cli.github.com/manual/gh_run_watch), [특정 실행의 artifact 받기](https://cli.github.com/manual/gh_run_download).

### 목적별 기존 workflow

위 명령의 workflow 파일명만 목적에 맞게 바꾼다. 아래 파일에는 모두 수동 실행 trigger가 있다.

| workflow 파일 | 용도 / 결과 범위 |
|---|---|
| `linux-vulkan-compile.yml` | 두 Linux Vulkan preset과 CPU 계약, `linux-vulkan-compile-<SHA>` artifact |
| `linux-vulkan-validation.yml` | 고정 VVL과 Mesa software ICD의 core/sync 통합 검증 |
| `linux-vulkan-draw.yml` | software color pass·draw 참조 |
| `linux-vulkan-timestamp.yml` | software ICD timestamp 계약; 실제 GPU 성능으로 해석하지 않음 |
| `linux-vulkan-package.yml` | 설치 SDK 소비자·입력·payload 감사 |
| `linux-xcb-matrix.yml` | Xvfb 기반 XCB 통합 출력 matrix |
| `linux-xcb-manual.yml` | 실제 화면 검증을 위한 준비 도구의 software smoke; 사람의 확인을 대신하지 않음 |
| `linux-comparison-probe.yml` | Linux SDK/SDL software 비교 준비 진단; 물리 GPU 성능 수용 아님 |

예를 들어 required validation은 다음처럼 별도로 요청하고, 그 실행 ID로 결과를 받는다.

```powershell
& $Gh workflow run linux-vulkan-validation.yml --repo $Repository --ref $TargetRef
```

## 4. Docker로 로컬 Linux 검사하기

아래는 **커밋된 로컬 HEAD**를 Linux 파일시스템에 새로 clone한다. Windows의 `out/`·CMake cache·`.deps/`·미커밋 파일을 Linux 빌드에 섞지 않는다. 저장소 bind mount는 읽기 전용이며 결과 폴더만 쓰기 가능하다. Docker build context도 작은 전용 폴더로 제한한다. [Docker build context](https://docs.docker.com/build/concepts/context/), [컨테이너 실행과 mount](https://docs.docker.com/engine/containers/run/).

### 4-1. 검사 이미지 만들기

저장소 root의 PowerShell에서 다음을 실행한다. 패키지와 공개 dependency를 내려받을 인터넷 연결이 필요하다. `ubuntu:24.04` tag와 배포 패키지는 갱신될 수 있으므로 실제 image ID와 compiler 버전을 결과와 함께 보존한다.

```powershell
$RepoPath = (Get-Location).Path
$Image = 'graphicssdk-linux-checks:ubuntu24'
$ToolingDir = Join-Path $RepoPath ('out\docker-tooling-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $ToolingDir | Out-Null
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Dockerfile = @'
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake git gcc-14 g++-14 ninja-build python3 make \
    libvulkan1 mesa-vulkan-drivers vulkan-tools \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-14 CXX=g++-14
WORKDIR /work
'@
[IO.File]::WriteAllText((Join-Path $ToolingDir 'Dockerfile'), $Dockerfile.Replace("`r`n", "`n"), $Utf8NoBom)
& $Docker build --platform linux/amd64 --tag $Image $ToolingDir
if ($LASTEXITCODE -ne 0) { throw '검사 이미지 빌드 실패' }
```

`rm`은 이미지 안의 apt cache만 정리한다. 호스트의 소스나 결과를 삭제하는 명령은 없다. 예시는 `linux/amd64` 기준이며 다른 architecture나 emulator의 결과를 같은 성능 표본으로 합치지 않는다.

### 4-2. 검사 종류를 선택하고 실행하기

`$Mode`만 바꿔 매번 새 컨테이너·새 결과 폴더에서 실행한다.

- `core`: backend OFF portable core 빌드·CPU CTest·전체 Python 검사.
- `compile`: 고정 Vulkan-Headers로 `linux-vulkan`/`linux-vulkan-validation` 빌드·CPU 계약. GPU 테스트는 실행하지 않는다.
- `validation`: 고정 VVL을 빌드하고 Mesa software ICD로 required core/sync suite와 오류 통제를 실행한다. 초기 준비가 오래 걸릴 수 있다.

```powershell
$Mode = 'compile' # core, compile, validation 중 하나
$ExpectedCommit = (git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw '저장소 root에서 실행하세요.' }
$EvidenceDir = Join-Path $RepoPath ("out\docker-$Mode-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $EvidenceDir | Out-Null

$Script = @'
#!/usr/bin/env bash
set -euo pipefail
collect() {
    result=$?
    trap - EXIT
    set +e
    collection_result=0
    for name in docker-python linux-vulkan-compile-evidence linux-vulkan-validation-evidence; do
        if test -d "/work/GraphicsSDK/out/$name"; then
            cp -a "/work/GraphicsSDK/out/$name" /evidence/ || collection_result=$?
        fi
    done
    if test -d /work/GraphicsSDK/out/portable/Testing; then
        cp -a /work/GraphicsSDK/out/portable/Testing /evidence/portable-Testing || collection_result=$?
    fi
    if test "$result" -eq 0 && test "$collection_result" -ne 0; then result=$collection_result; fi
    printf '%s\n' "$result" > /evidence/exit-code.txt || result=1
    exit "$result"
}
trap collect EXIT

git -c safe.directory=/source clone --no-local --depth 1 /source /work/GraphicsSDK
cd /work/GraphicsSDK
test "$(git rev-parse HEAD)" = "$GFX_TEST_COMMIT"
git rev-parse HEAD > /evidence/source-commit.txt
uname -a > /evidence/uname.txt
g++-14 --version > /evidence/compiler.txt
cmake --version > /evidence/cmake.txt

if test "$GFX_TEST_MODE" != core; then
    git init .deps/Vulkan-Headers
    git -C .deps/Vulkan-Headers remote add origin https://github.com/KhronosGroup/Vulkan-Headers.git
    git -C .deps/Vulkan-Headers fetch --depth 1 origin b379292b2ab6df5771ba9870d53cf8b2c9295daf
    git -C .deps/Vulkan-Headers checkout --detach FETCH_HEAD
fi

case "$GFX_TEST_MODE" in
    core)
        cmake --preset portable-core -DCMAKE_CXX_COMPILER=g++-14 -DGFX_ENABLE_VULKAN=OFF
        cmake --build --preset portable-core --parallel 1
        ctest --preset portable-core --no-tests=error
        python3 tools/check_python_contracts.py --output out/docker-python
        ;;
    compile)
        python3 tools/run_linux_compile.py --compiler g++-14
        ;;
    validation)
        python3 tools/prepare_linux_validation.py
        python3 tools/run_linux_validation.py
        ;;
    *) echo 'Unknown mode' >&2; exit 2 ;;
esac
'@
[IO.File]::WriteAllText((Join-Path $EvidenceDir 'run.sh'), $Script.Replace("`r`n", "`n"), $Utf8NoBom)
& $Docker image inspect $Image --format '{{.Id}}' | Set-Content (Join-Path $EvidenceDir 'image-id.txt')
& $Docker run --rm --platform linux/amd64 `
    --mount "type=bind,source=$RepoPath,target=/source,readonly" `
    --mount "type=bind,source=$EvidenceDir,target=/evidence" `
    --env "GFX_TEST_COMMIT=$ExpectedCommit" --env "GFX_TEST_MODE=$Mode" `
    $Image bash /evidence/run.sh 2>&1 | Tee-Object -FilePath (Join-Path $EvidenceDir 'docker.log')
$DockerExitCode = $LASTEXITCODE
if ($DockerExitCode -ne 0) { throw "Linux 검사 실패: $EvidenceDir 의 로그·종료 코드를 확인하세요." }
Write-Output "검사 결과: $EvidenceDir"
```

`--rm`으로 컨테이너가 종료돼도 호스트의 `$EvidenceDir`는 남는다. compiler/commit/종료 코드와 생성된 receipt·로그를 함께 본다. 프로세스를 강제로 종료하거나 daemon이 중단되면 trap이 실행되지 않을 수 있으므로 receipt가 없는 결과를 성공으로 취급하지 않는다. 원본 receipt 안의 컨테이너 내부 경로는 호스트 경로로 고쳐 쓰지 않는다.

이 경로는 **현재 작업 폴더의 미커밋 수정을 검사하지 않는다**. GitHub 경로와 비교하려면 `source-commit.txt`와 CI `headSha`가 같아야 한다. 소스 hash가 다른 Windows/Linux 기록을 같은 실행으로 합치지 않는다.

XCB 가상 화면 검사까지 필요한 경우에는 위 이미지에 임의 설정을 덧붙여 성공으로 간주하지 말고, 먼저 해당 기존 workflow의 `libxcb1-dev`·`xvfb`·`xauth`·`openbox`와 고정 `--xcb` validation 준비 절차를 그대로 따른다. 간단한 시작점은 3절의 `linux-xcb-matrix.yml`이다.

## 5. macOS 검사 추가 예시

현재 저장소에는 macOS workflow가 없다. 아래는 `.github/workflows/macos-portable.yml`로 **추가·커밋해 기본 branch인 main에 등록한 뒤** 사용할 수 있는 수동 실행 예시다. 이 문서에 예시를 실은 것과 macOS 검사가 이미 통과한 것은 다르다. runner label의 architecture는 실제 로그의 `uname -m`으로 확인한다. [GitHub runner OS·architecture 목록](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

```yaml
name: macOS portable CPU checks
on:
  workflow_dispatch:
permissions:
  contents: read
jobs:
  portable:
    runs-on: macos-15
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
        with:
          persist-credentials: false
      - name: Prepare compiler tools
        run: |
          uname -a
          clang++ --version
          cmake --version
          if ! command -v ninja >/dev/null; then brew install ninja; fi
      - run: cmake --preset portable-core -DCMAKE_CXX_COMPILER=clang++ -DGFX_ENABLE_D3D12=OFF -DGFX_ENABLE_VULKAN=OFF
      - run: cmake --build --preset portable-core --parallel 1
      - run: ctest --preset portable-core --no-tests=error --output-junit out/macos-cpu.xml
      - run: python3 tools/check_python_contracts.py --output out/python-contracts
      - uses: actions/upload-artifact@v4
        if: always()
        with:
          name: macos-portable-${{ github.sha }}
          path: |
            out/macos-cpu.xml
            out/portable/Testing/Temporary/
            out/python-contracts/
          if-no-files-found: warn
```

추가 후 `gh workflow run macos-portable.yml --repo $Repository --ref main`으로 실행한다. 감시·artifact 수집은 3절을 따른다. C++23/compiler·OS 차이로 실패하면 미대응 부분의 발견으로 기록한다. Metal·MoltenVK·macOS 화면 검증은 이 CPU 예시에 포함되지 않는다.

## 6. 실제 Linux GPU와 화면 수용

`linux-hardware-acceptance.yml`은 `self-hosted, Linux, X64, graphicssdk-gpu` label의 runner를 요구한다. 해당 runner를 별도로 등록하고, runner에 설치된 실제 ICD와 그 ICD로 조회한 정확한 device UUID를 지정한다.

```powershell
$LinuxIcd = Read-Host 'Linux runner의 ICD JSON 절대 경로'
$DeviceUuids = Read-Host '해당 ICD의 lowercase device UUID (여러 개면 쉼표로 구분)'
& $Gh workflow run linux-hardware-acceptance.yml --repo $Repository --ref main `
    -f "icd=$LinuxIcd" -f "device_uuids=$DeviceUuids"
```

runner가 미등록·오프라인이면 queued 상태로 남을 수 있다. 권한 부족과 runner 부재를 구분한다. software ICD 성공, Xvfb 이미지 일치, `--gpus all`의 계산 기능만으로 물리 Linux Vulkan이나 사람의 화면 확인을 완료 처리하지 않는다. Docker GPU 공개에는 backend/driver 조건이 있으며 CUDA 사용 가능 여부로 이 SDK의 Vulkan ICD·확장 지원을 추정하지 않는다. [Docker Windows GPU 기능](https://docs.docker.com/desktop/features/gpu/).

실제 표시·최소화/복원·resize는 기존 [Linux 화면 확인 준비 절차](PHASE3_LINUX_XCB_MANUAL_READINESS.md)와 `tools/linux_xcb_manual.py`의 review binding을 따른다. `P3-LNX-15`, `P3-WSI-08`, Linux 물리 GPU 비교의 미완료 항목은 실제 해당 증거를 확보한 뒤 완료 처리한다.

## 7. 자주 막히는 지점

| 증상 | 확인 사항 |
|---|---|
| `docker` / `gh`를 찾지 못함 | 새 터미널 또는 2절의 실행 파일 경로. 이번 사용자별 Docker 배치와 Program Files의 GitHub CLI는 위치가 다름 |
| Docker client는 동작하지만 server 연결 실패 | Docker Desktop 실행·선택 context·Linux engine을 확인하고 `docker info` 성공 여부 확인 |
| `docker-credential-desktop`를 찾지 못함 | 2절처럼 Docker `resources\bin`을 현재 터미널의 PATH에 포함. 로그인 정보를 삭제하거나 인증을 끄는 대신 보조 프로그램 경로 확인 |
| bind mount의 Windows 경로를 찾지 못함 | 로컬 `desktop-linux` context인지 확인. 원격 daemon에는 Windows 로컬 경로를 그대로 전달할 수 없음 |
| `workflow does not have workflow_dispatch` | `build.yml`은 push/PR 전용. 수동 실행을 지원하는 파일 선택 |
| GitHub CLI 인증/403 오류 | `gh auth status`와 대상 repo/Actions 권한 확인. 제한된 실행 환경에서만 실패하면 일반 사용자 터미널에서도 확인하고 인증 정보를 바로 폐기하지 않음 |
| fine-grained PAT에서 `gh run watch` 실패 | 현재 CLI 공식 문서의 인증 제한 확인. `gh run view <실행 ID>`로 상태를 확인하거나 지원되는 로그인 방식 사용 |
| 과거 성공 run이 조회됨 | workflow·SHA·event·실행 ID·생성 시각 대조 |
| `Evidence root already exists` | 새 컨테이너/checkout·출력 폴더 사용. 기존 증거를 지워 재사용하지 않음 |
| validation layer 부족 | 고정 runtime 준비와 실제 로드 경로 확인. required validation을 끄고 성공 처리하지 않음 |
| 컴파일만 성공함 | receipt의 GPU/validation/WSI 수용 flag를 확인하고 CPU 성공과 구분 |

기존 환경 조사·CI 보고서는 당시 관측 기록으로 보존한다. 이번 CLI 도입 상태는 2절에 별도로 기록하며, 과거의 CLI 미관측 기록을 현재 상태로 바꿔 쓰지 않는다.
