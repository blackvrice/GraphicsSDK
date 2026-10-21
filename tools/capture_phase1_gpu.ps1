param(
    [ValidateSet('resource_inflight','multi_upload','video_4k','video_p010')][string]$Scenario='resource_inflight',
    [ValidateRange(1,10000)][int]$Samples=120,
    [string]$Output=''
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
if (-not $Output) { $Output=Join-Path $repoRoot ('out/phase1-gpu-capture-'+(Get-Date -Format 'yyyyMMdd-HHmmss')) }
$capturePath=[IO.Path]::GetFullPath($Output)
$allowedRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'out'))+[IO.Path]::DirectorySeparatorChar
if (-not $capturePath.StartsWith($allowedRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Capture output must be inside this repository out directory' }
if (Test-Path -LiteralPath $capturePath) { throw 'Capture output exists; preserve original observations' }
New-Item -ItemType Directory -Path $capturePath | Out-Null
$status=& wpr -status 2>&1
$status | Set-Content -LiteralPath (Join-Path $capturePath 'before.log')
if (($status -join "`n") -notmatch 'not recording') { throw 'Another WPR recording may be active; leave it unchanged' }
$exe=Join-Path $repoRoot 'out/bench/Release/phase1_llgl.exe'
$binaryHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
$recorded=$false
try {
    & wpr -start GPU -filemode -recordtempto $capturePath *> (Join-Path $capturePath 'start.log')
    if ($LASTEXITCODE) { throw 'WPR GPU capture failed; see start.log. No system policy or privilege was changed.' }
    $recorded=$true
    $child=Start-Process -FilePath $exe -ArgumentList @($Scenario,$Samples,'0') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $capturePath 'child.stdout') -RedirectStandardError (Join-Path $capturePath 'child.log')
    $childPid=$child.Id
    if (-not $child.WaitForExit(120000)) { Stop-Process -Id $childPid; throw 'Own benchmark capture timed out' }
    $child.Refresh()
    @{pid=$childPid;scenario=$Scenario;samples=$Samples;executable_sha256=$binaryHash;exit_code=$child.ExitCode;captured_at_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $capturePath 'process.json')
    if ($child.ExitCode) { throw 'Captured benchmark failed; preserve trace for diagnostics' }
} finally {
    if ($recorded) {
        & wpr -stop (Join-Path $capturePath 'gpu.etl') *> (Join-Path $capturePath 'stop.log')
        if ($LASTEXITCODE) { Write-Warning 'WPR stop failed; inspect recording status before any next capture' }
    }
    & wpr -status *> (Join-Path $capturePath 'after.log')
}
Write-Output $capturePath
