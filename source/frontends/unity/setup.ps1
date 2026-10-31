<#
Checks that this PC can build one Unity front-end variant and writes the environment the build needs (UNITY_EXE) to
-EnvFile for build.bat to load. The editor must be the version in ProjectSettings\ProjectVersion.txt. What it cannot
find on its own it asks for: the folder of an editor installed somewhere it does not look, or to install the editor
and its build support module through the Unity Hub. A folder given by hand can be kept for later builds as a user
environment variable. Exits 0 when every required piece is present.
#>
param(
    [Parameter(Mandatory = $true)][string]$Variant,
    [string]$EnvFile,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
# Unattended machines (test runners) set BALLS_SETUP_YES=1 so build.bat installs what is missing without asking.
if ($env:BALLS_SETUP_YES -eq '1') { $Yes = [switch]$true }
$project = $PSScriptRoot
$interactive = [Environment]::UserInteractive -and -not [Console]::IsInputRedirected

$versionFile = Get-Content -LiteralPath (Join-Path $project 'ProjectSettings\ProjectVersion.txt')
$version = ($versionFile | Select-String '^m_EditorVersion:\s*(\S+)').Matches[0].Groups[1].Value
$changeset = ($versionFile | Select-String '^m_EditorVersionWithRevision:\s*\S+\s*\((\w+)\)').Matches |
    ForEach-Object { $_.Groups[1].Value } | Select-Object -First 1

# The player support each variant needs: the folder under Editor\Data\PlaybackEngines, and the Unity Hub module that
# installs it (the Windows Mono player comes with every Windows editor).
switch ($Variant) {
    'win64' { $playbackEngine = 'WindowsStandaloneSupport'; $module = $null }
    'linux' { $playbackEngine = 'LinuxStandaloneSupport'; $module = 'linux-il2cpp' }
    default {
        Write-Host "  setup.ps1 does not know the variant '$Variant'." -ForegroundColor Red
        exit 2
    }
}

$environment = [ordered]@{}
$failures = 0

function Report([string]$name, [bool]$ok, [string]$detail) {
    $mark = if ($ok) { ' ok ' } else { 'MISS' }
    $color = if ($ok) { 'Green' } else { 'Red' }
    Write-Host ('  [{0}] {1,-22} {2}' -f $mark, $name, $detail) -ForegroundColor $color
}

function Confirm([string]$question) {
    if ($Yes) { return $true }
    if (-not $interactive) { return $false }
    $answer = Read-Host "    $question [Y/n]"
    return ($answer -eq '' -or $answer -match '^[Yy]')
}

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user;$env:Path"
}

function Get-EditorVersion([string]$exe) {
    # Editor\Data\..\..\ is the version folder the Hub and the installer name; the file version is the fallback.
    $info = (Get-Item -LiteralPath $exe).VersionInfo
    $folder = Split-Path (Split-Path (Split-Path $exe -Parent) -Parent) -Leaf
    if ($folder -match '^\d+\.\d+\.\d+[abfp]\d+$') { return $folder }
    return $info.ProductVersion
}

# Accepts Unity.exe, its Editor folder or the version folder above it.
function Resolve-Editor([string]$path) {
    if (-not $path) { return $null }
    foreach ($candidate in @($path, (Join-Path $path 'Unity.exe'), (Join-Path $path 'Editor\Unity.exe'))) {
        if ((Test-Path -LiteralPath $candidate -PathType Leaf) -and (Split-Path $candidate -Leaf) -eq 'Unity.exe') {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Find-Hub {
    $keys = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*', 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
    $entry = Get-ItemProperty $keys -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -like 'Unity Hub*' } |
        Select-Object -First 1
    $candidates = @()
    if ($entry -and $entry.InstallLocation) { $candidates += Join-Path $entry.InstallLocation 'Unity Hub.exe' }
    if ($entry -and $entry.DisplayIcon) { $candidates += ($entry.DisplayIcon -split ',')[0].Trim('"') }
    $candidates += "$env:ProgramFiles\Unity Hub\Unity Hub.exe"
    return $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) -and $_ -like '*Unity Hub.exe' } | Select-Object -First 1
}

# Every place an editor of this version may be: the installer's registry entry, the Hub's install folders (its
# default and the one set in its preferences) and the usual folders on each drive.
function Find-Editor {
    $candidates = [System.Collections.Generic.List[string]]::new()
    foreach ($hive in 'HKLM', 'HKCU') {
        $key = Get-ItemProperty "${hive}:\SOFTWARE\Unity Technologies\Installer\Unity $version" -ErrorAction SilentlyContinue
        if ($key -and $key.'Location x64') { $candidates.Add((Join-Path $key.'Location x64' 'Editor\Unity.exe')) }
    }
    $secondary = Join-Path $env:APPDATA 'UnityHub\secondaryInstallPath.json'
    if (Test-Path -LiteralPath $secondary) {
        $folder = (Get-Content -Raw -LiteralPath $secondary | ConvertFrom-Json)
        if ($folder) { $candidates.Add((Join-Path $folder "$version\Editor\Unity.exe")) }
    }
    $candidates.Add("$env:ProgramFiles\Unity\Hub\Editor\$version\Editor\Unity.exe")
    foreach ($drive in Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -match '^[A-Z]:\\$' }) {
        foreach ($folder in 'Unity', 'Unity\Hub\Editor', 'Program Files\Unity\Hub\Editor', 'Unity\Editor') {
            $candidates.Add((Join-Path $drive.Root "$folder\$version\Editor\Unity.exe"))
        }
    }
    return $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}

function Install-Hub {
    $hub = Find-Hub
    if ($hub) { return $hub }
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if (-not $winget) {
        Write-Host '    The Unity Hub is not installed and winget is not available; get it from' -ForegroundColor Yellow
        Write-Host '    https://unity.com/download, then run this again.' -ForegroundColor Yellow
        return $null
    }
    if (-not (Confirm 'Install the Unity Hub with winget now?')) { return $null }
    & $winget.Source install --id Unity.UnityHub --exact --accept-package-agreements --accept-source-agreements
    Update-SessionPath
    return Find-Hub
}

# The Hub's command line installs an editor and its modules without its window; Unity's terms and sign-in are the
# Hub's to ask for on first use.
function Invoke-Hub([string]$hub, [string[]]$arguments) {
    # Started from an Electron app's terminal (an IDE), the Hub inherits ELECTRON_RUN_AS_NODE and runs as bare Node.js.
    Remove-Item Env:ELECTRON_RUN_AS_NODE -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $hub -ArgumentList (@('--', '--headless') + $arguments) -Wait -PassThru -NoNewWindow
    return $process.ExitCode -eq 0
}

function Install-Editor {
    $hub = Install-Hub
    if (-not $hub) { return }
    $arguments = @('install', '--version', $version)
    if ($changeset) { $arguments += @('--changeset', $changeset) }
    if ($module) { $arguments += @('--module', $module) }
    Write-Host "    Installing Unity $version through the Unity Hub (several GB; this takes a while)..."
    if (-not (Invoke-Hub $hub $arguments)) {
        Write-Host "    The Hub could not install it from the command line. Open the Unity Hub > Installs > Install Editor," -ForegroundColor Yellow
        Write-Host "    pick $version (Archive > download archive for older ones), then run this again." -ForegroundColor Yellow
    }
}

function Install-Module([string]$editor) {
    $hub = Install-Hub
    if (-not $hub) { return }
    Write-Host "    Adding the $module module to Unity $version through the Unity Hub..."
    if (-not (Invoke-Hub $hub @('install-modules', '--version', $version, '--module', $module))) {
        Write-Host "    The Hub could not add it from the command line. In the Unity Hub > Installs, open the gear next to" -ForegroundColor Yellow
        Write-Host "    $version > Add modules and tick Linux Build Support (IL2CPP)." -ForegroundColor Yellow
    }
}

function Get-Editor {
    if ($env:UNITY_EXE) {
        $exe = Resolve-Editor $env:UNITY_EXE
        if ($exe) { return $exe }
        Write-Host "    UNITY_EXE is set to '$env:UNITY_EXE', which is not a Unity.exe." -ForegroundColor Yellow
    }
    $exe = Find-Editor
    if (-not $exe -and $Yes) {
        # Unattended (-Yes): nobody to ask, so install the project's editor through the Hub straight away.
        Install-Editor
        return Find-Editor
    }
    while (-not $exe) {
        if (-not $interactive) { return $null }
        Write-Host "    Unity $version was not found. You can:" -ForegroundColor Yellow
        Write-Host '      [f] give the folder it is installed in (or its Unity.exe)'
        Write-Host '      [i] install it with the Unity Hub'
        Write-Host '      [s] skip (the build stops here)'
        $answer = ''
        while ($answer -notin 'f', 'i', 's') { $answer = (Read-Host '    Choice [f/i/s]').Trim().ToLower() }
        switch ($answer) {
            'f' {
                $exe = Resolve-Editor ((Read-Host '    Folder or Unity.exe').Trim().Trim('"'))
                if (-not $exe) {
                    Write-Host '    No Unity.exe there.' -ForegroundColor Yellow
                } elseif ((Get-EditorVersion $exe) -ne $version) {
                    # Opening the project in another version upgrades or downgrades it, which rewrites tracked files.
                    $found = Get-EditorVersion $exe
                    if (-not (Confirm "That editor is $found, not $version; the project would be reimported in it. Use it anyway?")) {
                        $exe = $null
                    }
                }
                if ($exe -and (Confirm 'Remember it for later builds (sets the user environment variable UNITY_EXE)?')) {
                    [Environment]::SetEnvironmentVariable('UNITY_EXE', $exe, 'User')
                }
            }
            'i' {
                Install-Editor
                $exe = Find-Editor
            }
            default { return $null }
        }
    }
    return $exe
}

Write-Host "Checking this PC can build the Unity $Variant player..."
$editor = Get-Editor
Report "Unity $version" ([bool]$editor) $(if ($editor) { $editor } else { 'install it with the Unity Hub, or set UNITY_EXE to its Unity.exe' })
if ($editor) {
    $environment['UNITY_EXE'] = $editor
    $playback = Join-Path (Split-Path $editor -Parent) "Data\PlaybackEngines\$playbackEngine"
    $hasPlayback = Test-Path -LiteralPath $playback
    if (-not $hasPlayback -and $module -and ($Yes -or $interactive) -and (Confirm "Add $playbackEngine ($module) to Unity $version with the Unity Hub now?")) {
        Install-Module $editor
        $hasPlayback = Test-Path -LiteralPath $playback
    }
    Report $playbackEngine $hasPlayback $(if ($hasPlayback) { 'player support' } else { "add the build support module to Unity $version in the Unity Hub" })
    if (-not $hasPlayback) { $failures++ }
} else {
    $failures++
}

$cmake = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue
Report 'CMake' ([bool]$cmake) $(if ($cmake) { 'builds the native plugins' } else { 'install CMake 3.25+ or run a Visual Studio developer prompt' })
if (-not $cmake) { $failures++ }

if ($Variant -eq 'linux') {
    $plugins = Join-Path $project 'NativeLibraries\linux-x86_64\libballs_unity.so'
    $hasPlugins = Test-Path -LiteralPath $plugins
    Report 'Linux native plugins' $hasPlugins $(if ($hasPlugins) { 'source\frontends\unity\NativeLibraries\linux-x86_64' } else { 'build them on Linux: cmake -S . -B build/linux-unity -DBALLS_FRONTEND=none -DBALLS_PROFILE=extended, then copy build/linux-unity/bin/*.so into source\frontends\unity\NativeLibraries\linux-x86_64' })
    if (-not $hasPlugins) { $failures++ }
}

if ($EnvFile) {
    $lines = @('@echo off')
    foreach ($name in $environment.Keys) { $lines += "set `"$name=$($environment[$name])`"" }
    New-Item -ItemType Directory -Force -Path (Split-Path $EnvFile -Parent) | Out-Null
    Set-Content -Path $EnvFile -Value $lines -Encoding ASCII
}

if ($failures -gt 0) {
    Write-Host "This PC is missing $failures thing(s) needed to build the Unity $Variant player; see MISS above." -ForegroundColor Red
    exit 1
}
Write-Host "Ready to build the Unity $Variant player." -ForegroundColor Green
exit 0
