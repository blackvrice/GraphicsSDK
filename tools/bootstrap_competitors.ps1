param([int]$Jobs = 8)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $pins = Get-Content -LiteralPath 'benchmarks/competitors/sources.lock.json' -Raw | ConvertFrom-Json
    foreach ($item in @(@{Key='llgl'; Directory='LLGL'}, @{Key='sdl'; Directory='SDL'})) {
        $pin = $pins.($item.Key)
        $path = Join-Path '.deps' $item.Directory
        if (-not (Test-Path -LiteralPath $path)) {
            New-Item -ItemType Directory -Path $path -Force | Out-Null
            git -C $path init
            if ($LASTEXITCODE) { throw 'git init failed' }
            git -C $path remote add origin ('https://github.com/' + $pin.repository + '.git')
            if ($LASTEXITCODE) { throw 'remote add failed' }
            git -C $path fetch --depth 1 origin $pin.commit
            if ($LASTEXITCODE) { throw 'Pinned fetch failed' }
            git -C $path checkout --detach $pin.commit
            if ($LASTEXITCODE) { throw 'Pinned checkout failed' }
        }
        $head = git -C $path rev-parse HEAD
        $dirty = git -C $path status --porcelain --untracked-files=no
        if ($head -ne $pin.commit -or $dirty) { throw "Existing $path differs from pin; preserve it and use a fresh source directory" }
    }
    cmake -S .deps/LLGL -B out/llgl -G 'Visual Studio 17 2022' -A x64 -DLLGL_BUILD_RENDERER_OPENGL=OFF -DLLGL_BUILD_RENDERER_DIRECT3D11=OFF -DLLGL_BUILD_RENDERER_NULL=OFF -DLLGL_BUILD_TESTS=OFF -DLLGL_BUILD_EXAMPLES=OFF -DLLGL_ENABLE_DEBUG_LAYER=OFF
    if ($LASTEXITCODE) { throw 'LLGL configure failed' }
    cmake --build out/llgl --config Release --parallel $Jobs
    if ($LASTEXITCODE) { throw 'LLGL build failed' }
    cmake -S .deps/SDL -B out/sdl -G 'Visual Studio 17 2022' -A x64 -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_AUDIO=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF -DSDL_CAMERA=OFF -DSDL_DIALOG=OFF
    if ($LASTEXITCODE) { throw 'SDL configure failed' }
    cmake --build out/sdl --config Release --parallel $Jobs
    if ($LASTEXITCODE) { throw 'SDL build failed' }
    cmake -S benchmarks -B out/bench -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE) { throw 'Benchmark configure failed' }
    cmake --build out/bench --config Release --parallel $Jobs
    if ($LASTEXITCODE) { throw 'Benchmark build failed' }
} finally { Pop-Location }
