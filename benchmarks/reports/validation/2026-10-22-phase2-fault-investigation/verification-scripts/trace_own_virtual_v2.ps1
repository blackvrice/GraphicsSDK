$ErrorActionPreference='Stop'
$repoRoot='D:\source\GraphicsSDK';$destination=Join-Path $repoRoot 'out/gbv-origin-virtual-traces-v2';$receipt=Join-Path $repoRoot 'out/gbv-origin-virtual-status-v2.json'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Private trace requires elevation'}
if(Test-Path -LiteralPath $destination){throw 'Refuse existing output'}
New-Item -ItemType Directory -Path $destination | Out-Null
$xperf='C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe'
$exe=Join-Path $repoRoot 'out/gbv-allocation-origin/build/Release/gbv_allocation_origin.exe'
$controller=Join-Path $repoRoot 'out/private-virtual-controller/build/Release/private_virtual_controller.exe'
$hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;$controllerHash=(Get-FileHash -LiteralPath $controller -Algorithm SHA256).Hash
$rows=@();$failure=$null
try { foreach($index in @(0,1)){foreach($mode in @('full','no-tracking')){
  $folder=Join-Path $destination "adapter-$index-$mode";New-Item -ItemType Directory -Path $folder | Out-Null
  $prefix=Join-Path $folder 'handshake';$memory=Join-Path $folder 'memory.jsonl';$etl=Join-Path $folder 'virtual.etl'
  $options=[Diagnostics.ProcessStartInfo]::new();$options.FileName=$exe;$options.Arguments="$mode $index 2 256 `"$memory`" `"$prefix`"";$options.UseShellExecute=$false;$options.CreateNoWindow=$true;$options.RedirectStandardOutput=$true;$options.RedirectStandardError=$true
  $child=[Diagnostics.Process]::new();$child.StartInfo=$options;if(-not $child.Start()){throw 'Own native child failed'}
  $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync()
  $session='GraphicsSDKPrivateVirtual-'+[Guid]::NewGuid().ToString('N');$traceReady=Join-Path $folder 'trace.ready';$traceStop=Join-Path $folder 'trace.stop';$monitor=$null;$traceOut=$null;$traceError=$null
  $row=@{adapter_index=$index;mode=$mode;pid=$child.Id;session=$session;executable_sha256=$hash;controller_sha256=$controllerHash;nt_kernel_logger_touched=$false;registry_changed=$false;raw_system_metadata_local_only=$true};$rows+=,$row
  try {
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while(-not(Test-Path -LiteralPath ($prefix+'.ready'))){if($child.HasExited -or [DateTime]::UtcNow -gt $deadline){throw 'Native handshake failed'};Start-Sleep -Milliseconds 50}
    $controlOptions=[Diagnostics.ProcessStartInfo]::new();$controlOptions.FileName=$controller;$controlOptions.Arguments="`"$session`" `"$etl`" `"$traceReady`" `"$traceStop`"";$controlOptions.UseShellExecute=$false;$controlOptions.CreateNoWindow=$true;$controlOptions.RedirectStandardOutput=$true;$controlOptions.RedirectStandardError=$true
    $monitor=[Diagnostics.Process]::new();$monitor.StartInfo=$controlOptions;if(-not $monitor.Start()){throw 'Private controller failed to launch'}
    $traceOut=$monitor.StandardOutput.ReadToEndAsync();$traceError=$monitor.StandardError.ReadToEndAsync()
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while(-not(Test-Path -LiteralPath $traceReady)){if($monitor.HasExited -or [DateTime]::UtcNow -gt $deadline){throw 'Private controller did not start its own session'};Start-Sleep -Milliseconds 50}
    [IO.File]::WriteAllText($prefix+'.go','go')
    if(-not $child.WaitForExit(60000)){$child.Kill();throw 'Own native child timeout'};$child.WaitForExit();$row.exit_code=$child.ExitCode
    [IO.File]::WriteAllText($traceStop,'stop')
    if(-not $monitor.WaitForExit(15000)){throw 'Private controller stop timeout'};$monitor.WaitForExit();$row.controller_exit=$monitor.ExitCode
    if($child.ExitCode -ne 0 -or $monitor.ExitCode -ne 0){throw 'Own child/controller failed'}
  } finally {
    if(-not $child.HasExited){$child.Kill();$child.WaitForExit()}
    if($monitor){
      if(-not $monitor.HasExited){[IO.File]::WriteAllText($traceStop,'stop');if(-not $monitor.WaitForExit(15000)){& $xperf -stop $session *> (Join-Path $folder 'own-session-fallback-stop.log');$row.fallback_stop_exit=$LASTEXITCODE;$monitor.Kill();$monitor.WaitForExit()}}
      [IO.File]::WriteAllText((Join-Path $folder 'controller.json'),$traceOut.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false));[IO.File]::WriteAllText((Join-Path $folder 'controller-error.log'),$traceError.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
    }
    [IO.File]::WriteAllText((Join-Path $folder 'stdout.log'),$stdout.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false));[IO.File]::WriteAllText((Join-Path $folder 'stderr.log'),$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
    $rows | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'cases.json') -Encoding utf8
  }
  & $xperf -i $etl -o (Join-Path $folder 'virtual-events.csv') -a dumper *> (Join-Path $folder 'export.log');$row.export_exit=$LASTEXITCODE
  if($LASTEXITCODE){throw 'Private virtual export failed'}
}}
if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $hash -or (Get-FileHash -LiteralPath $controller -Algorithm SHA256).Hash -ne $controllerHash){throw 'Frozen executable changed'}
} catch {$failure=$_.Exception.Message} finally {
  @{completed=$true;error=$failure;processes=$rows;nt_kernel_logger_touched=$false;registry_changed=$false;raw_system_metadata_local_only=$true;end_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $receipt -Encoding utf8
}
if($failure){throw $failure}
