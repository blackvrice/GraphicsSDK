param([string]$Output='',[ValidateRange(1,10000)][int]$Samples=120)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'This capture requires a user-approved elevated Windows process. It does not change policy or group membership.' }
if (-not $Output) { $Output=Join-Path $repoRoot ('out/phase1-gpu-lifetime-'+(Get-Date -Format 'yyyyMMdd-HHmmss')) }
$capturePath=[IO.Path]::GetFullPath($Output)
$allowedRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'out'))+[IO.Path]::DirectorySeparatorChar
if (-not $capturePath.StartsWith($allowedRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Output must be inside repository out' }
if (Test-Path -LiteralPath $capturePath) { throw 'Preserve existing capture output' }
$control=Join-Path $repoRoot 'out/gpu-trace/Release/gpu_memory_control.exe'
$benchmark=Join-Path $repoRoot 'out/bench/Release/phase1_llgl.exe'
$exporter=Join-Path $repoRoot 'out/gpu-trace/Release/gpu_etl_export.exe'
foreach($exe in @($control,$benchmark,$exporter)) { if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing build: $exe" } }
$xperf=(Get-Command xperf.exe -ErrorAction Stop).Source
New-Item -ItemType Directory -Path $capturePath | Out-Null
$session='GraphicsSDKMemory-'+[Guid]::NewGuid().ToString('N')
$jobs=@(@{name='positive_control';exe=$control;args=@()})
foreach($name in @('resource_inflight','multi_upload','video_4k','video_p010')) { $jobs+=@{name=$name;exe=$benchmark;args=@($name,"$Samples",'0','--cpu-only')} }
$records=@();$started=$false;$stopped=$false
try {
    # Resource(0x40), Memory(0x80), References(0x4), Base(0x1). Private file session;
    # no kernel/global WPR session is started, stopped, canceled, or reconfigured.
    & $xperf -start $session -on '802ec45a-1e99-4b83-9920-87c98277ba9d:0xc5:5' -BufferSize 1024 -MinBuffers 32 -MaxBuffers 128 -f (Join-Path $capturePath 'gpu.etl') *> (Join-Path $capturePath 'start.log')
    if($LASTEXITCODE) { throw 'Private GPU ETW session failed to start; see start.log' }
    $started=$true
    foreach($job in $jobs) {
        $options=[Diagnostics.ProcessStartInfo]::new();$options.FileName=$job.exe;$options.WorkingDirectory=Split-Path $job.exe
        $options.Arguments=$job.args -join ' ';$options.UseShellExecute=$false;$options.CreateNoWindow=$true
        $options.RedirectStandardOutput=$true;$options.RedirectStandardError=$true
        $begin=[DateTime]::UtcNow.ToString('o');$child=[Diagnostics.Process]::new();$child.StartInfo=$options
        if(-not $child.Start()) { throw 'Own benchmark failed to launch' };$childPid=$child.Id
        $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync()
        if(-not $child.WaitForExit(120000)) { $child.Kill();throw 'Own benchmark process timed out' }
        $child.WaitForExit();$exitCode=$child.ExitCode
        [IO.File]::WriteAllText((Join-Path $capturePath ($job.name+'.stdout')),$stdout.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $capturePath ($job.name+'.log')),$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
        $records+=@{name=$job.name;pid=$childPid;exe=$job.exe;arguments=$job.args;exit_code=$exitCode;start_utc=$begin;end_utc=[DateTime]::UtcNow.ToString('o');executable_sha256=(Get-FileHash -LiteralPath $job.exe -Algorithm SHA256).Hash.ToLowerInvariant()}
        if($exitCode -ne 0) { throw "Captured workload failed: $($job.name)" }
    }
    Start-Sleep -Milliseconds 500
} finally {
    if($started) { & $xperf -stop $session *> (Join-Path $capturePath 'stop.log');$stopped=($LASTEXITCODE -eq 0) }
    @{revision=1;session=$session;provider='802ec45a-1e99-4b83-9920-87c98277ba9d';keywords='0xc5';session_started=$started;session_stopped=$stopped;processes=$records;exporter_sha256=(Get-FileHash -LiteralPath $exporter -Algorithm SHA256).Hash.ToLowerInvariant()} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $capturePath 'capture.json') -Encoding utf8
}
if(-not $stopped) { throw "Own ETW stop failed; inspect session $session before any new capture" }
& $exporter (Join-Path $capturePath 'gpu.etl') (Join-Path $capturePath 'events.jsonl') (Join-Path $capturePath 'trace.json') *> (Join-Path $capturePath 'export.log')
if($LASTEXITCODE) { throw 'ETL decode/loss check failed; preserve raw trace and export.log' }
Write-Output $capturePath
