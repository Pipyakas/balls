# Prints the root of an Unreal Engine 5.8 install, for build.bat when UE_ROOT is not set. Looks at the engine fork's
# usual place, the installs and source builds Epic's launcher registers, and the folders a stock 5.8 install is
# usually unpacked to on each drive.
$ErrorActionPreference = 'SilentlyContinue'

function Test-Engine58([string]$root) {
    if (-not $root -or -not (Test-Path -LiteralPath (Join-Path $root 'Engine\Build\BatchFiles\RunUAT.bat'))) {
        return $false
    }
    $version = Get-Content -Raw -LiteralPath (Join-Path $root 'Engine\Build\Build.version') | ConvertFrom-Json
    return $version.MajorVersion -eq 5 -and $version.MinorVersion -eq 8
}

$candidates = [System.Collections.Generic.List[string]]::new()
$candidates.Add('D:\Unreal\UnrealEngine')
$installed = Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8'
if ($installed.InstalledDirectory) { $candidates.Add($installed.InstalledDirectory) }
$builds = Get-ItemProperty 'HKCU:\Software\Epic Games\Unreal Engine\Builds'
if ($builds) {
    $builds.PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' } | ForEach-Object { $candidates.Add($_.Value) }
}
foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
    foreach ($folder in 'UE_5.8', 'Unreal\UE_5.8', 'Unreal\UnrealEngine', 'Program Files\Epic Games\UE_5.8') {
        $candidates.Add((Join-Path $drive.Root $folder))
    }
}

$found = $candidates | Where-Object { Test-Engine58 $_ } | Select-Object -First 1
if ($found) {
    Write-Output ([IO.Path]::GetFullPath($found).TrimEnd('\'))
    exit 0
}
exit 1
