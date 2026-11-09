param(
 [string]$Workbench = 'C:\Program Files (x86)\Steam\steamapps\common\Arma Reforger Tools\Workbench\ArmaReforgerWorkbenchSteamDiag.exe',
 [string]$GameAddons = 'C:\Program Files (x86)\Steam\steamapps\common\Arma Reforger\addons'
)
$ErrorActionPreference = 'Stop'
$addon = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $addon
$run = Join-Path ([IO.Path]::GetTempPath()) ('SHS_IFF_Check_' + [guid]::NewGuid().ToString('N'))
$cases = @(
 @{ Name='defaults'; Json=$null; Mask=255; Effective='friendly=1 hostile=1 civilian=1 unknown=1 friendlyOutline=1 hostileOutline=1' },
 @{ Name='master-off'; Json='{"iffEnabled":false}'; Mask=0; Effective='friendly=0 hostile=0 civilian=0 unknown=0 friendlyOutline=0 hostileOutline=0' },
 @{ Name='no-outlines'; Json='{"friendlyOutlinesEnabled":false,"hostileOutlinesEnabled":false}'; Mask=159; Effective='friendly=1 hostile=1 civilian=1 unknown=1 friendlyOutline=0 hostileOutline=0' },
 @{ Name='no-friendly'; Json='{"friendlyIffEnabled":false}'; Mask=253; Effective='friendly=0 hostile=1 civilian=1 unknown=1 friendlyOutline=0 hostileOutline=1' },
 @{ Name='no-sharing'; Json='{"contactSharingEnabled":false}'; Mask=127; Effective='friendly=1 hostile=1 civilian=1 unknown=1 friendlyOutline=1 hostileOutline=1' },
 @{ Name='no-hostile'; Json='{"hostileTrackingEnabled":false}'; Mask=251; Effective='friendly=1 hostile=0 civilian=1 unknown=1 friendlyOutline=1 hostileOutline=0' },
 @{ Name='no-noncombatant'; Json='{"civilianIffEnabled":false,"unknownContactsEnabled":false}'; Mask=231; Effective='friendly=1 hostile=1 civilian=0 unknown=0 friendlyOutline=1 hostileOutline=1' },
 @{ Name='invalid'; Json='{ broken json'; Mask=0; Effective='friendly=0 hostile=0 civilian=0 unknown=0 friendlyOutline=0 hostileOutline=0' }
)
foreach ($case in $cases) {
 $folder = Join-Path $run $case.Name
 $profile = Join-Path $folder 'profile'
 New-Item -ItemType Directory -Force -Path $profile | Out-Null
 $effectiveProfile = Join-Path $profile 'profile'
 New-Item -ItemType Directory -Force -Path $effectiveProfile | Out-Null
 $config = Join-Path $effectiveProfile 'SHS_IVAS_IFF.json'
 if ($null -ne $case.Json) { [IO.File]::WriteAllText($config, $case.Json) }
 $roots = @($GameAddons, $repo) -join ','
 $arguments = @('-gproj', ('"' + (Join-Path $addon 'addon.gproj') + '"'), '-addonsDir', ('"' + $roots + '"'), '-profile', ('"' + $profile + '"'), '-logsDir', ('"' + $folder + '"'), '-noThrow', '-wbmodule=ResourceManager', '-run', '-plugin=SHS_IffPolicyValidation')
 $proc = Start-Process -FilePath $Workbench -WorkingDirectory (Split-Path -Parent $Workbench) -ArgumentList $arguments -WindowStyle Hidden -PassThru
 if (!$proc.WaitForExit(60000)) { Stop-Process -Id $proc.Id; throw "Timeout: $folder" }
 $log = Get-Content -LiteralPath (Join-Path $folder 'console.log') -Raw
 $unexpectedScriptErrors = @($log -split '\r?\n' | Where-Object { $_ -match 'SCRIPT\s+\(E\)' -and !($case.Name -eq 'invalid' -and $_ -match '\[SHS IVAS IFF\] Invalid SHS_IVAS_IFF.json; IFF disabled') }).Count -gt 0
 $linkExpected = 'LINK share=1 receive=1'
 if ($case.Name -in @('master-off', 'invalid', 'no-sharing', 'no-hostile')) { $linkExpected = 'LINK share=0 receive=0' }
 if (!$log.Contains($linkExpected)) { throw "Data link gates failed: $folder" }
 if ($proc.ExitCode -ne 0 -or $unexpectedScriptErrors -or $log -notmatch ('\[SHS IFF TEST\] MASK=' + $case.Mask + '(\r?\n|\s*$)') -or !$log.Contains($case.Effective)) { throw "FAIL $($case.Name): inspect $folder/console.log" }
 if ($null -ne $case.Json -and [IO.File]::ReadAllText($config) -cne $case.Json) { throw "Existing config overwritten: $config" }
 if ($null -eq $case.Json) {
  $created = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
  if ($created.iffEnabled -ne $true -or $created.contactSharingEnabled -ne $true) { throw "Defaults not generated: $config" }
 }
 Write-Output "PASS $($case.Name): policy + effective device gates + config preservation"
}
Write-Output "Evidence: $run"
Write-Output 'Dedicated-server replication still requires multiplayer testing.'


