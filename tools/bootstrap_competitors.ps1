param([int]$Jobs = 8, [switch]$Diligent, [switch]$Bgfx, [switch]$Forge)
$ErrorActionPreference = 'Stop'
if (([int]$Diligent.IsPresent + [int]$Bgfx.IsPresent + [int]$Forge.IsPresent) -gt 1) { throw 'Select one reviewed optional build profile: -Diligent, -Bgfx or -Forge' }
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $pins = Get-Content -LiteralPath 'benchmarks/competitors/sources.lock.json' -Raw | ConvertFrom-Json
    $sources = @(@{Key='llgl'; Directory='LLGL'}, @{Key='sdl'; Directory='SDL'})
    if ($Diligent) { $sources += @{Key='diligent_core'; Directory='DiligentCore'} }
    if ($Forge) {
        $forgePin=(Get-Content -LiteralPath 'benchmarks/competitors/forge-build.lock.json' -Raw | ConvertFrom-Json).dependencies.the_forge
        if ($forgePin.commit -ne $pins.the_forge.commit) { throw 'Research and Forge build pins differ' }
        $sources += @{Key='the_forge'; Directory='TheForge'; Pin=$forgePin}
    }
    if ($Bgfx) {
        $bgfxPins = (Get-Content -LiteralPath 'benchmarks/competitors/bgfx-build.lock.json' -Raw | ConvertFrom-Json).dependencies
        if ($bgfxPins.bgfx.commit -ne $pins.bgfx.commit) { throw 'Research and bgfx build pins differ' }
        foreach ($key in @('bgfx','bx','bimg')) { $sources += @{Key=$key; Directory=$key; Pin=$bgfxPins.$key} }
    }
    foreach ($item in $sources) {
        $pin = if ($item.ContainsKey('Pin')) { $item.Pin } else { $pins.($item.Key) }
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
    if ($Forge) {
        cmake -S benchmarks -B out/bench-forge -G 'Visual Studio 17 2022' -A x64 -DGFX_BENCH_WITH_FORGE=ON -DGFX_BENCH_WITH_BGFX=OFF -DGFX_BENCH_WITH_DILIGENT=OFF
        if ($LASTEXITCODE) { throw 'Forge benchmark configure failed' }
        cmake --build out/bench-forge --config Release --target gfx_bench --parallel $Jobs
        if ($LASTEXITCODE) { throw 'Forge benchmark build failed' }
        ctest --test-dir out/bench-forge -C Release --output-on-failure
        if ($LASTEXITCODE) { throw 'Forge native validation/readback tests failed' }
    } elseif ($Bgfx) {
        cmake -S benchmarks -B out/bench-bgfx -G 'Visual Studio 17 2022' -A x64 -DGFX_BENCH_WITH_BGFX=ON -DGFX_BENCH_WITH_DILIGENT=OFF
        if ($LASTEXITCODE) { throw 'bgfx benchmark configure failed' }
        cmake --build out/bench-bgfx --config Release --target gfx_bench bgfx_shader_contracts --parallel $Jobs
        if ($LASTEXITCODE) { throw 'bgfx benchmark build failed' }
        ctest --test-dir out/bench-bgfx -C Release --output-on-failure
        if ($LASTEXITCODE) { throw 'bgfx shader contract tests failed' }
    } elseif ($Diligent) {
        $buildPins = (Get-Content -LiteralPath 'benchmarks/competitors/diligent-build.lock.json' -Raw | ConvertFrom-Json).dependencies
        foreach ($key in @('diligent_xxhash','diligent_abseil','diligent_nvapi')) {
            $dep = $buildPins.$key
            if (Test-Path -LiteralPath (Join-Path $dep.directory '.git')) {
                $head = git -C $dep.directory rev-parse HEAD
                $dirty = git -C $dep.directory status --porcelain --untracked-files=no
                if ($head -ne $dep.commit -or $dirty) { throw "Existing $($dep.directory) differs from pin; preserve it and use a fresh build/source directory" }
            }
        }
        git -C .deps/DiligentCore submodule update --init --depth 1 ThirdParty/xxHash
        if ($LASTEXITCODE) { throw 'Diligent xxHash checkout failed' }
        # Configure fetches upstream's fixed Abseil and full NVAPI pins; CMake verifies all four SHAs.
        cmake -S benchmarks -B out/bench-diligent -G 'Visual Studio 17 2022' -A x64 -DGFX_BENCH_WITH_DILIGENT=ON
        if ($LASTEXITCODE) { throw 'Diligent benchmark configure failed' }
        cmake --build out/bench-diligent --config Release --target gfx_bench --parallel $Jobs
        if ($LASTEXITCODE) { throw 'Diligent benchmark build failed' }
    } else {
        cmake -S benchmarks -B out/bench -G 'Visual Studio 17 2022' -A x64
        if ($LASTEXITCODE) { throw 'Benchmark configure failed' }
        cmake --build out/bench --config Release --parallel $Jobs
        if ($LASTEXITCODE) { throw 'Benchmark build failed' }
    }
} finally { Pop-Location }
