$ErrorActionPreference='Stop'
$repoRoot='D:\source\GraphicsSDK'
$destination=Join-Path $repoRoot 'out/gbv-origin-virtual-traces'
$receipt=Join-Path $repoRoot 'out/gbv-origin-virtual-status.json'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Own-process virtual allocation trace needs an elevated process'}
if(Test-Path -LiteralPath $destination){throw 'Refuse existing output'}
if(-not(Test-Path -LiteralPath (Join-Path $repoRoot 'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation/retention-controls/inputs-after.json'))){throw 'Serial GPU predecessor has not completed'}
New-Item -ItemType Directory -Path $destination | Out-Null
$xperf='C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe'
$exe=Join-Path $repoRoot 'out/gbv-allocation-origin/build/Release/gbv_allocation_origin.exe'
$exeHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$rows=@();$failed=$null
try {
  foreach($adapterIndex in @(0,1)) { foreach($mode in @('full','no-tracking')) {
    $folder=Join-Path $destination ("adapter-$adapterIndex-$mode");New-Item -ItemType Directory -Path $folder | Out-Null
    $prefix=Join-Path $folder 'handshake';$memory=Join-Path $folder 'memory.jsonl';$etl=Join-Path $folder 'virtual.etl'
    $options=[Diagnostics.ProcessStartInfo]::new();$options.FileName=$exe;$options.WorkingDirectory=$repoRoot;$options.UseShellExecute=$false;$options.CreateNoWindow=$true
    $options.Arguments="$mode $adapterIndex 2 256 `"$memory`" `"$prefix`"";$options.RedirectStandardOutput=$true;$options.RedirectStandardError=$true
    $child=[Diagnostics.Process]::new();$child.StartInfo=$options;if(-not $child.Start()){throw 'Own native child failed to start'}
    $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync()
    $session='GraphicsSDKPrivateVirtual-'+[Guid]::NewGuid().ToString('N');$started=$false;$stopped=$false
    $row=@{adapter_index=$adapterIndex;mode=$mode;pid=$child.Id;session=$session;executable_sha256=$exeHash;registry_changed=$false;nt_kernel_logger_touched=$false;private_system_session=$true;raw_system_metadata_local_only=$true;target='own child; raw system-session metadata stays local; only target allocation rows are published'}
    $rows+=,$row
    try {
      $deadline=[DateTime]::UtcNow.AddSeconds(15)
      while(-not(Test-Path -LiteralPath ($prefix+'.ready'))){if($child.HasExited -or [DateTime]::UtcNow -gt $deadline){throw 'Own child did not reach trace handshake'};Start-Sleep -Milliseconds 50}
      & $xperf -start $session -on PROC_THREAD+LOADER+VIRT_ALLOC -BufferSize 1024 -MinBuffers 16 -MaxBuffers 32 -MaxFile 256 -stackwalk VirtualAlloc+VirtualFree -f $etl *> (Join-Path $folder 'start.log')
      if($LASTEXITCODE){throw 'Own PID private virtual session failed to start'};$started=$true
      [IO.File]::WriteAllText($prefix+'.go','go')
      if(-not $child.WaitForExit(60000)){$child.Kill();throw 'Own native child timed out'};$child.WaitForExit();$row.exit_code=$child.ExitCode
      if($child.ExitCode -ne 0){throw 'Own native child failed'}
    } finally {
      if(-not $child.HasExited){$child.Kill();$child.WaitForExit()}
      if($started){& $xperf -stop $session *> (Join-Path $folder 'stop.log');$stopped=($LASTEXITCODE -eq 0)}
      $row.session_started=$started;$row.session_stopped=$stopped
      [IO.File]::WriteAllText((Join-Path $folder 'stdout.log'),$stdout.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
      [IO.File]::WriteAllText((Join-Path $folder 'stderr.log'),$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
      $rows | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'cases.json') -Encoding utf8
    }
    if(-not $stopped){throw 'Own private virtual session stop failed'}
    # Keep raw stack addresses and resolve them against the same child's own
    # module map. No kernel logger or network symbols are needed for this export.
    & $xperf -i $etl -o (Join-Path $folder 'virtual-events.csv') -a dumper *> (Join-Path $folder 'export.log')
    $row.export_exit=$LASTEXITCODE
    if($LASTEXITCODE){throw 'Own heap event export failed'}
  }}
  if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $exeHash){throw 'Frozen native executable changed'}
} catch { $failed=$_.Exception.Message } finally {
  @{completed=$true;error=$failed;processes=$rows;output=$destination;registry_changed=$false;nt_kernel_logger_touched=$false;private_system_session=$true;raw_system_metadata_local_only=$true;end_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $receipt -Encoding utf8
}
if($failed){throw $failed}
