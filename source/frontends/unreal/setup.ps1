<#
Checks that this PC can build one Unreal front-end variant and writes the environment the build needs (UE_ROOT,
LINUX_MULTIARCH_ROOT) to -EnvFile for build.bat to load. What it cannot find on its own it asks for: the folder of an
engine or toolchain installed somewhere it does not look, or to install it (Unreal Engine through the Epic Games
Launcher, the Linux toolchain with Epic's installer). A folder given by hand can be kept for later builds as a user
environment variable. Exits 0 when every required piece is present.
#>
param(
    [Parameter(Mandatory = $true)][string]$Variant,
    [string]$EnvFile,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
# Stock Unreal Engine 5.8.4 is the engine every machine builds with; an older 5.8 hotfix is updated in the launcher.
$engineVersion = '5.8'
$enginePatch = 4
# The cross toolchain Unreal 5.8 builds Linux targets with, and Epic's installer for it.
$linuxToolchain = 'v26_clang-20.1.8-rockylinux8'
$linuxToolchainInstaller = "https://cdn.unrealengine.com/CrossToolchain_Linux/$linuxToolchain.exe"
$defaultLinuxToolchainRoot = "D:\UnrealToolchains\$linuxToolchain\"
$interactive = [Environment]::UserInteractive -and -not [Console]::IsInputRedirected

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

# Offers the ways to get a missing piece; returns the letter picked, or '' when there is nobody to ask.
function Ask-Choice([string]$what, [string]$installText) {
    if (-not $interactive) { return '' }
    Write-Host "    $what was not found. You can:" -ForegroundColor Yellow
    Write-Host "      [f] give the folder it is installed in"
    Write-Host "      [i] $installText"
    Write-Host "      [s] skip (the build stops here)"
    while ($true) {
        $answer = (Read-Host '    Choice [f/i/s]').Trim().ToLower()
        if ($answer -in 'f', 'i', 's') { return $answer }
    }
}

function Read-Folder([string]$prompt) {
    $answer = (Read-Host "    $prompt").Trim().Trim('"')
    if (-not $answer) { return $null }
    return [IO.Path]::GetFullPath($answer)
}

# Keeps a folder given by hand for later builds and new consoles.
function Remember([string]$name, [string]$value) {
    if (Confirm "Remember it for later builds (sets the user environment variable $name)?") {
        [Environment]::SetEnvironmentVariable($name, $value, 'User')
    }
}

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user;$env:Path"
}

function Test-Engine([string]$root) {
    if (-not $root -or -not (Test-Path -LiteralPath (Join-Path $root 'Engine\Build\BatchFiles\RunUAT.bat'))) {
        return $false
    }
    try {
        $version = Get-Content -Raw -LiteralPath (Join-Path $root 'Engine\Build\Build.version') | ConvertFrom-Json
    } catch {
        return $false
    }
    return "$($version.MajorVersion).$($version.MinorVersion)" -eq $engineVersion -and $version.PatchVersion -ge $enginePatch
}

# Accepts the engine folder itself or any folder inside it (Engine, Engine\Binaries\Win64, ...).
function Resolve-Engine([string]$folder) {
    $current = $folder
    while ($current) {
        if (Test-Engine $current) { return $current.TrimEnd('\') }
        $current = Split-Path $current -Parent
    }
    return $null
}

function Find-Engine {
    $found = & (Join-Path $PSScriptRoot 'FindUnrealEngine.ps1')
    if ($LASTEXITCODE -eq 0 -and $found) { return "$found".Trim() }
    # A last, wider look: engine folders up to two levels below each drive's root and its Epic Games folders.
    foreach ($drive in Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Free -ne $null -and $_.Root -match '^[A-Z]:\\$' }) {
        $bases = @($drive.Root, (Join-Path $drive.Root 'Program Files\Epic Games'), (Join-Path $drive.Root 'Epic Games'))
        foreach ($base in $bases) {
            $folders = Get-ChildItem -LiteralPath $base -Directory -Depth 1 -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^(UE_?5|UnrealEngine)' }
            foreach ($folder in $folders) {
                if (Test-Engine $folder.FullName) { return $folder.FullName }
            }
        }
    }
    return $null
}

function Find-EpicLauncher {
    foreach ($candidate in @(
            "${env:ProgramFiles(x86)}\Epic Games\Launcher\Portal\Binaries\Win64\EpicGamesLauncher.exe",
            "$env:ProgramFiles\Epic Games\Launcher\Portal\Binaries\Win64\EpicGamesLauncher.exe",
            "${env:ProgramFiles(x86)}\Epic Games\Launcher\Portal\Binaries\Win32\EpicGamesLauncher.exe")) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

# The launcher has no command line for installing an engine: open it on the Unreal Engine library and wait for the
# user, then look again.
function Install-WithLauncher([string]$what, [string]$steps) {
    if (-not (Find-EpicLauncher)) {
        $winget = Get-Command winget -ErrorAction SilentlyContinue
        if (-not $winget) {
            Write-Host '    The Epic Games Launcher is not installed and winget is not available; get it from' -ForegroundColor Yellow
            Write-Host '    https://store.epicgames.com/download, then run this again.' -ForegroundColor Yellow
            return
        }
        if (-not (Confirm 'Install the Epic Games Launcher with winget now?')) { return }
        & $winget.Source install --id EpicGames.EpicGamesLauncher --exact --accept-package-agreements --accept-source-agreements
        Update-SessionPath
    }
    Start-Process 'com.epicgames.launcher://ue/library'
    Write-Host "    The Epic Games Launcher is opening on Unreal Engine > Library. $steps" -ForegroundColor Yellow
    Read-Host "    Press Enter once $what has finished installing" | Out-Null
}

function Get-Engine {
    if ($env:UE_ROOT) {
        $root = Resolve-Engine $env:UE_ROOT
        if ($root) { return $root }
        Write-Host "    UE_ROOT is set to '$env:UE_ROOT', which is not an Unreal Engine $engineVersion.$enginePatch folder." -ForegroundColor Yellow
    }
    $root = Find-Engine
    while (-not $root) {
        $choice = Ask-Choice "Unreal Engine $engineVersion" 'install it with the Epic Games Launcher'
        switch ($choice) {
            'f' {
                $folder = Read-Folder "Folder of Unreal Engine $engineVersion (the one that contains Engine\)"
                $root = if ($folder) { Resolve-Engine $folder }
                if ($root) {
                    Remember 'UE_ROOT' $root
                } else {
                    Write-Host "    No Unreal Engine $engineVersion there." -ForegroundColor Yellow
                }
            }
            'i' {
                Install-WithLauncher "Unreal Engine $engineVersion" `
                    "Press + next to Engine Versions (or Install Engine), pick $engineVersion and install it (or update an installed $engineVersion to $engineVersion.$enginePatch)."
                $root = Find-Engine
            }
            default { return $null }
        }
    }
    return $root
}

# A stock install carries a target platform only if the launcher's option for it was ticked; a source build compiles
# what it needs.
function Test-LinuxTarget([string]$root) {
    if (-not (Test-Path -LiteralPath (Join-Path $root 'Engine\Build\InstalledBuild.txt'))) { return $true }
    return Test-Path -LiteralPath (Join-Path $root 'Engine\Binaries\Linux\UnrealGame.target')
}

function Test-LinuxToolchain([string]$root) {
    return $root -and (Test-Path -LiteralPath (Join-Path $root 'x86_64-unknown-linux-gnu\bin\clang++.exe'))
}

function Get-LinuxToolchain {
    # Epic's installer sets LINUX_MULTIARCH_ROOT machine-wide, which a console opened before it does not see yet.
    $candidates = @($env:LINUX_MULTIARCH_ROOT,
        [Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'User'),
        [Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'Machine'),
        $defaultLinuxToolchainRoot)
    foreach ($drive in Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -match '^[A-Z]:\\$' }) {
        $candidates += Join-Path $drive.Root "UnrealToolchains\$linuxToolchain\"
    }
    $root = $candidates | Where-Object { Test-LinuxToolchain $_ } | Select-Object -First 1
    while (-not $root) {
        $choice = Ask-Choice "Unreal's Linux cross toolchain ($linuxToolchain)" "download and run Epic's installer for it"
        switch ($choice) {
            'f' {
                $folder = Read-Folder "Folder of the $linuxToolchain toolchain (the one that contains x86_64-unknown-linux-gnu\)"
                if (Test-LinuxToolchain $folder) {
                    $root = $folder
                    Remember 'LINUX_MULTIARCH_ROOT' ($folder.TrimEnd('\') + '\')
                } else {
                    Write-Host '    No toolchain there.' -ForegroundColor Yellow
                }
            }
            'i' {
                $installer = Join-Path $env:TEMP "$linuxToolchain.exe"
                Write-Host "    Downloading $linuxToolchainInstaller ..."
                Invoke-WebRequest -Uri $linuxToolchainInstaller -OutFile $installer -UseBasicParsing
                Start-Process -FilePath $installer -Wait
                Remove-Item -LiteralPath $installer -ErrorAction SilentlyContinue
                $root = @([Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'Machine'),
                    [Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'User'),
                    $defaultLinuxToolchainRoot) | Where-Object { Test-LinuxToolchain $_ } | Select-Object -First 1
            }
            default { return $null }
        }
    }
    return $root.TrimEnd('\') + '\'
}

Write-Host "Checking this PC can build the Unreal $Variant game..."
if ($Variant -notin 'win64', 'linux') {
    Write-Host "  setup.ps1 does not know the variant '$Variant'." -ForegroundColor Red
    exit 2
}

$engine = Get-Engine
Report "Unreal Engine $engineVersion" ([bool]$engine) $(if ($engine) { $engine } else { 'install it with the Epic Games Launcher, or set UE_ROOT to its folder' })
if ($engine) {
    $environment['UE_ROOT'] = $engine
} else {
    $failures++
}

if ($Variant -eq 'linux') {
    if ($engine) {
        $linuxTarget = Test-LinuxTarget $engine
        while (-not $linuxTarget -and $interactive -and (Confirm "This engine has no Linux target platform. Add it in the Epic Games Launcher now?")) {
            Install-WithLauncher 'the Linux target platform' `
                "Open the arrow next to $engineVersion > Options, tick Linux under Target Platforms and Apply."
            $linuxTarget = Test-LinuxTarget $engine
        }
        Report 'Linux target platform' $linuxTarget $(if ($linuxTarget) { 'installed with the engine' } else { "in the Epic Games Launcher: $engineVersion > Options > Target Platforms > Linux" })
        if (-not $linuxTarget) { $failures++ }
    }
    $toolchain = Get-LinuxToolchain
    Report 'Linux toolchain' ([bool]$toolchain) $(if ($toolchain) { $toolchain } else { "install $linuxToolchain from $linuxToolchainInstaller, or set LINUX_MULTIARCH_ROOT" })
    if ($toolchain) {
        $environment['LINUX_MULTIARCH_ROOT'] = $toolchain
    } else {
        $failures++
    }
}

$cmake = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue
Report 'CMake' ([bool]$cmake) $(if ($cmake) { 'builds the core and the physics plugins' } else { 'install CMake 3.25+ or run a Visual Studio developer prompt' })
if (-not $cmake) { $failures++ }

if ($EnvFile) {
    $lines = @('@echo off')
    foreach ($name in $environment.Keys) { $lines += "set `"$name=$($environment[$name])`"" }
    New-Item -ItemType Directory -Force -Path (Split-Path $EnvFile -Parent) | Out-Null
    Set-Content -Path $EnvFile -Value $lines -Encoding ASCII
}

if ($failures -gt 0) {
    Write-Host "This PC is missing $failures thing(s) needed to build the Unreal $Variant game; see MISS above." -ForegroundColor Red
    exit 1
}
Write-Host "Ready to build the Unreal $Variant game." -ForegroundColor Green
exit 0
