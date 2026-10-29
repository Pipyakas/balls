<#
Checks that this PC can build one SDL front-end variant, offers to install whatever is missing, and writes the
environment the build needs (tool folders that are not on PATH, JAVA_HOME, ANDROID_HOME) to -EnvFile for
build.bat to load. Exits 0 when every required piece is present.
#>
param(
    [Parameter(Mandatory = $true)][string]$Variant,
    [string]$EnvFile,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$minimumCmake = [version]'3.25'
$minimumJdk = 17
$ndkVersion = '27.2.12479018'
$androidPlatform = 'android-35'
# The SDK's own CMake package that is known to build this project; any CMake 3.25+ works.
$androidCmakePackage = 'cmake;4.0.2'
$interactive = [Environment]::UserInteractive -and -not [Console]::IsInputRedirected

$pathAdditions = New-Object System.Collections.Generic.List[string]
$environment = [ordered]@{}
$failures = 0

function Report([string]$name, [bool]$ok, [string]$detail) {
    $mark = if ($ok) { ' ok ' } else { 'MISS' }
    $color = if ($ok) { 'Green' } else { 'Red' }
    Write-Host ('  [{0}] {1,-22} {2}' -f $mark, $name, $detail) -ForegroundColor $color
}

function Warn([string]$name, [string]$detail) {
    Write-Host ('  [warn] {0,-22} {1}' -f $name, $detail) -ForegroundColor Yellow
}

function Confirm-Install([string]$what) {
    if ($Yes) { return $true }
    if (-not $interactive) { return $false }
    $answer = Read-Host "    Install $what now? [Y/n]"
    return ($answer -eq '' -or $answer -match '^[Yy]')
}

# winget installs land in folders the current console's PATH does not know about yet.
function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user;$env:Path"
}

function Install-WithWinget([string]$what, [string]$id, [string[]]$extra = @()) {
    $winget = Find-Exe 'winget' @("$env:LOCALAPPDATA\Microsoft\WindowsApps\winget.exe")
    if (-not $winget) {
        Write-Host "    winget is not available; install $what by hand." -ForegroundColor Yellow
        return $false
    }
    if (-not (Confirm-Install $what)) { return $false }
    & $winget install --id $id --exact --accept-package-agreements --accept-source-agreements @extra
    Update-SessionPath
    return $true
}

function Use-Tool([string]$exe) {
    $folder = Split-Path $exe -Parent
    $onPath = $env:Path -split ';' | Where-Object { $_ -and ((Resolve-Path $_ -ErrorAction SilentlyContinue).Path -eq $folder) }
    if (-not $onPath) {
        $pathAdditions.Add($folder)
        $env:Path = "$folder;$env:Path"
    }
}

function Find-Exe([string]$name, [string[]]$fallbacks = @()) {
    $command = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue |
        Where-Object { $_.Source -notmatch 'WindowsApps' } | Select-Object -First 1
    if ($command) { return $command.Source }
    foreach ($candidate in $fallbacks) {
        $match = Get-Item $candidate -ErrorAction SilentlyContinue | Select-Object -Last 1
        if ($match) { return $match.FullName }
    }
    return $null
}

function Get-CmakeVersion([string]$exe) {
    $line = (& $exe --version 2>$null | Select-Object -First 1)
    if ($line -match '(\d+\.\d+(\.\d+)?)') { return [version]$Matches[1] }
    return $null
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath -latest
    if ($path) { return $path.Trim() }
    return $null
}

function Find-WindowsSdk {
    $roots = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue
    if (-not $roots -or -not $roots.KitsRoot10) { return $null }
    $include = Join-Path $roots.KitsRoot10 'Include'
    $latest = Get-ChildItem $include -Directory -Filter '10.*' -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'um\d3d12.h') } | Sort-Object Name | Select-Object -Last 1
    if ($latest) { return $latest.Name }
    return $null
}

function Test-Internet([string]$url) {
    try {
        Invoke-WebRequest -Uri $url -Method Head -TimeoutSec 8 -UseBasicParsing | Out-Null
        return $true
    } catch {
        return $false
    }
}

function Check-Cmake {
    $exe = Find-Exe 'cmake' @(
        "$env:ProgramFiles\CMake\bin\cmake.exe"
    )
    if (-not $exe -and $script:visualStudio) {
        $bundled = Join-Path $script:visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $bundled) { $exe = $bundled }
    }
    $version = if ($exe) { Get-CmakeVersion $exe } else { $null }
    if ($version -and $version -ge $minimumCmake) {
        Use-Tool $exe
        Report 'CMake' $true "$version ($exe)"
        return
    }
    $found = if ($version) { "found $version, " } else { '' }
    Report 'CMake' $false "${found}needs $minimumCmake or newer"
    if (Install-WithWinget 'CMake' 'Kitware.CMake') {
        $exe = Find-Exe 'cmake' @("$env:ProgramFiles\CMake\bin\cmake.exe")
        if ($exe -and (Get-CmakeVersion $exe) -ge $minimumCmake) {
            Use-Tool $exe
            Report 'CMake' $true "installed ($exe)"
            return
        }
    }
    $script:failures++
}

function Check-Git {
    $exe = Find-Exe 'git' @("$env:ProgramFiles\Git\cmd\git.exe")
    if ($exe) {
        Use-Tool $exe
        Report 'Git' $true '(CMake clones NVRHI with it)'
        return
    }
    Report 'Git' $false 'CMake clones NVRHI with it'
    if (Install-WithWinget 'Git' 'Git.Git') {
        $exe = Find-Exe 'git' @("$env:ProgramFiles\Git\cmd\git.exe")
        if ($exe) {
            Use-Tool $exe
            Report 'Git' $true "installed ($exe)"
            return
        }
    }
    $script:failures++
}

function Check-Desktop([string]$buildDir) {
    $script:visualStudio = Find-VisualStudio
    $sdk = Find-WindowsSdk
    if ($script:visualStudio -and $sdk) {
        Report 'Visual Studio 2022' $true "C++ tools in $script:visualStudio"
        Report 'Windows SDK' $true $sdk
    } else {
        if (-not $script:visualStudio) { Report 'Visual Studio 2022' $false 'needs the "Desktop development with C++" workload' }
        if (-not $sdk) { Report 'Windows SDK' $false 'needs a Windows 10/11 SDK with Direct3D 12 headers' }
        $override = '--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
        if (Install-WithWinget 'Visual Studio 2022 Build Tools with the C++ workload (several GB)' `
                'Microsoft.VisualStudio.2022.BuildTools' @('--override', $override)) {
            $script:visualStudio = Find-VisualStudio
            $sdk = Find-WindowsSdk
        }
        if (-not ($script:visualStudio -and $sdk)) { $script:failures++ }
    }
    Check-Cmake
    Check-Git

    $python = Find-Exe 'python'
    $cudaReady = Test-Path (Join-Path $root '.deps\cuda\sdk\include\nvrtc.h')
    if ($cudaReady) {
        Report 'CUDA compiler pieces' $true '.deps\cuda'
    } elseif ($python) {
        Use-Tool $python
        Report 'Python' $true 'fetches the CUDA compiler pieces on the first build'
    } else {
        Warn 'Python' 'missing: the build skips the CUDA backend (the CPU backends still build)'
        if (Install-WithWinget 'Python 3' 'Python.Python.3.12') {
            $python = Find-Exe 'python'
            if ($python) { Use-Tool $python; Report 'Python' $true "installed ($python)" }
        }
    }

    if (-not (Test-Path (Join-Path $root "$buildDir\_deps"))) {
        $online = Test-Internet 'https://github.com'
        Report 'Internet' $online 'the first configure downloads SDL, Dear ImGui and NVRHI from github.com'
        if (-not $online) { $script:failures++ }
    }
}

function Get-JavaMajor([string]$exe) {
    # java prints its version on stderr, which Windows PowerShell turns into an error under 'Stop'.
    $ErrorActionPreference = 'Continue'
    $text = (& $exe -version 2>&1 | Out-String)
    if ($text -match 'version "(\d+)(\.(\d+))?') {
        if ($Matches[1] -eq '1') { return [int]$Matches[3] }
        return [int]$Matches[1]
    }
    return 0
}

function Check-Jdk {
    $candidates = @()
    if ($env:JAVA_HOME) { $candidates += (Join-Path $env:JAVA_HOME 'bin\java.exe') }
    $onPath = Find-Exe 'java'
    if ($onPath) { $candidates += $onPath }
    $candidates += Get-Item "$env:ProgramFiles\Microsoft\jdk-*\bin\java.exe", "$env:ProgramFiles\Eclipse Adoptium\jdk-*\bin\java.exe", `
        "$env:ProgramFiles\Java\jdk-*\bin\java.exe" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
    foreach ($java in $candidates) {
        if ((Test-Path $java) -and (Get-JavaMajor $java) -ge $minimumJdk) {
            $javaHome = Split-Path (Split-Path $java -Parent) -Parent
            if ($env:JAVA_HOME -ne $javaHome) { $environment['JAVA_HOME'] = $javaHome; $env:JAVA_HOME = $javaHome }
            Report 'JDK' $true "$(Get-JavaMajor $java) ($javaHome)"
            return
        }
    }
    Report 'JDK' $false "needs JDK $minimumJdk or newer (Gradle runs on it)"
    if (Install-WithWinget 'Microsoft OpenJDK 17' 'Microsoft.OpenJDK.17') {
        $java = Get-Item "$env:ProgramFiles\Microsoft\jdk-17*\bin\java.exe" -ErrorAction SilentlyContinue | Select-Object -Last 1
        if ($java) {
            $javaHome = Split-Path (Split-Path $java.FullName -Parent) -Parent
            $environment['JAVA_HOME'] = $javaHome
            $env:JAVA_HOME = $javaHome
            Report 'JDK' $true "installed ($javaHome)"
            return
        }
    }
    $script:failures++
}

function Find-AndroidSdk {
    $candidates = @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)
    $properties = Join-Path $PSScriptRoot 'android\local.properties'
    if (Test-Path $properties) {
        $line = Get-Content $properties | Where-Object { $_ -match '^sdk\.dir=' } | Select-Object -First 1
        if ($line) { $candidates += ($line -replace '^sdk\.dir=', '' -replace '\\:', ':' -replace '\\\\', '\') }
    }
    $candidates += (Join-Path $env:LOCALAPPDATA 'Android\Sdk')
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path (Join-Path $candidate 'platform-tools'))) { return (Resolve-Path $candidate).Path }
        if ($candidate -and (Test-Path (Join-Path $candidate 'cmdline-tools'))) { return (Resolve-Path $candidate).Path }
    }
    return $null
}

function Find-SdkManager([string]$sdk) {
    $manager = Get-Item (Join-Path $sdk 'cmdline-tools\*\bin\sdkmanager.bat') -ErrorAction SilentlyContinue |
        Sort-Object { if ($_.FullName -match '\\latest\\') { 1 } else { 0 } } | Select-Object -Last 1
    if ($manager) { return $manager.FullName }
    return $null
}

function Check-Android {
    Check-Jdk
    $sdk = Find-AndroidSdk
    if (-not $sdk) {
        Report 'Android SDK' $false 'set ANDROID_HOME, or install Android Studio (winget id Google.AndroidStudio) and open it once'
        $script:failures++
        return
    }
    if ($env:ANDROID_HOME -ne $sdk) { $environment['ANDROID_HOME'] = $sdk; $env:ANDROID_HOME = $sdk }
    Report 'Android SDK' $true $sdk

    $missing = @()
    $ndk = Test-Path (Join-Path $sdk "ndk\$ndkVersion\source.properties")
    Report 'Android NDK' $ndk $ndkVersion
    if (-not $ndk) { $missing += "ndk;$ndkVersion" }
    $platform = Test-Path (Join-Path $sdk "platforms\$androidPlatform")
    Report 'Android platform' $platform $androidPlatform
    if (-not $platform) { $missing += "platforms;$androidPlatform" }
    $sdkCmake = Get-ChildItem (Join-Path $sdk 'cmake') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+(\.\d+)+$' -and [version]$_.Name -ge $minimumCmake } | Select-Object -Last 1
    Report 'Android CMake' ([bool]$sdkCmake) $(if ($sdkCmake) { $sdkCmake.Name } else { "needs $minimumCmake or newer in the SDK" })
    if (-not $sdkCmake) { $missing += $androidCmakePackage }

    if ($missing.Count -gt 0) {
        $manager = Find-SdkManager $sdk
        if (-not $manager) {
            Write-Host "    No sdkmanager in $sdk\cmdline-tools; install these with Android Studio's SDK Manager: $($missing -join ', ')" -ForegroundColor Yellow
            $script:failures++
        } elseif (Confirm-Install ($missing -join ', ')) {
            & $manager --sdk_root="$sdk" --install @missing
            if ($LASTEXITCODE -ne 0) { $script:failures++ }
        } else {
            $script:failures++
        }
    }

    $gradleCached = Test-Path (Join-Path $env:USERPROFILE '.gradle\wrapper\dists')
    if (-not $gradleCached -or -not (Test-Path (Join-Path $PSScriptRoot 'android\app\.cxx'))) {
        $online = Test-Internet 'https://services.gradle.org'
        Report 'Internet' $online 'Gradle, its plugins and the native dependencies download on the first build'
        if (-not $online) { $script:failures++ }
    }
}

Write-Host "Checking this PC can build $Variant..."
switch -Regex ($Variant) {
    '^win32-scoring$' { Check-Desktop 'build\win32-scoring' }
    '^win32-nvrhi$' { Check-Desktop 'build\win32' }
    '^win32-(webgpu|sdlgpu)$' { Check-Desktop "build\$Variant" }
    '^android$' { Check-Android }
    default { Write-Host "  setup.ps1 does not know the variant '$Variant'." -ForegroundColor Red; exit 2 }
}

if ($EnvFile) {
    $lines = @('@echo off')
    foreach ($name in $environment.Keys) { $lines += "set `"$name=$($environment[$name])`"" }
    if ($pathAdditions.Count -gt 0) { $lines += "set `"PATH=$($pathAdditions -join ';');%PATH%`"" }
    New-Item -ItemType Directory -Force -Path (Split-Path $EnvFile -Parent) | Out-Null
    Set-Content -Path $EnvFile -Value $lines -Encoding ASCII
}

if ($failures -gt 0) {
    Write-Host "This PC is missing $failures thing(s) needed to build $Variant; see MISS above." -ForegroundColor Red
    exit 1
}
Write-Host "Ready to build $Variant." -ForegroundColor Green
exit 0
