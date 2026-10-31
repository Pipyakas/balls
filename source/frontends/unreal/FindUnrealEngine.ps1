# Prints the root of an Unreal Engine 5.8 install, for build.bat when UE_ROOT is not set. Stock installs come first:
# the launcher's own record of what it installed, its registry entry, and the folders a stock 5.8 install is usually
# unpacked to on each drive. Source builds (the engine fork included) are used only when no stock 5.8 is found: a
# source engine compiles the whole engine into every worktree's game target, while a stock one links it prebuilt.
$ErrorActionPreference = 'SilentlyContinue'

function Test-Engine58([string]$root) {
    if (-not $root -or -not (Test-Path -LiteralPath (Join-Path $root 'Engine\Build\BatchFiles\RunUAT.bat'))) {
        return $false
    }
    $version = Get-Content -Raw -LiteralPath (Join-Path $root 'Engine\Build\Build.version') | ConvertFrom-Json
    return $version.MajorVersion -eq 5 -and $version.MinorVersion -eq 8
}

function Test-Installed([string]$root) {
    return Test-Path -LiteralPath (Join-Path $root 'Engine\Build\InstalledBuild.txt')
}

$candidates = [System.Collections.Generic.List[string]]::new()
$launcher = Get-Content -Raw -LiteralPath (Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat') |
    ConvertFrom-Json
if ($launcher) {
    $launcher.InstallationList | Where-Object { $_.AppName -eq 'UE_5.8' } | ForEach-Object { $candidates.Add($_.InstallLocation) }
}
$installed = Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8'
if ($installed.InstalledDirectory) { $candidates.Add($installed.InstalledDirectory) }
foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
    foreach ($folder in 'UE_5.8', 'Unreal\UE_5.8', 'Program Files\Epic Games\UE_5.8') {
        $candidates.Add((Join-Path $drive.Root $folder))
    }
}
$builds = Get-ItemProperty 'HKCU:\Software\Epic Games\Unreal Engine\Builds'
if ($builds) {
    $builds.PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' } | ForEach-Object { $candidates.Add($_.Value) }
}
$candidates.Add('D:\Unreal\UnrealEngine')
foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
    $candidates.Add((Join-Path $drive.Root 'Unreal\UnrealEngine'))
}

$engines = $candidates | Where-Object { Test-Engine58 $_ }
$found = @($engines | Where-Object { Test-Installed $_ }) + @($engines) | Select-Object -First 1
if ($found) {
    Write-Output ([IO.Path]::GetFullPath($found).TrimEnd('\'))
    exit 0
}
exit 1
