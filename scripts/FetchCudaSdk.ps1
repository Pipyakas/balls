param(
    [string]$Destination = (Join-Path $PSScriptRoot '..\.deps\cuda'),
    [string]$CudaMajor = '12',
    [switch]$ProfilerTools
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Expand-Wheel([string]$wheel, [string]$destination) {
    [System.IO.Compression.ZipFile]::ExtractToDirectory($wheel, $destination)
}
$wheelDir = Join-Path $Destination 'wheels'
New-Item -ItemType Directory -Force $wheelDir | Out-Null

$packages = @("nvidia-cuda-nvrtc-cu$CudaMajor", "nvidia-cuda-runtime-cu$CudaMajor")
python -m pip download --no-deps --only-binary=:all: --platform win_amd64 @packages -d $wheelDir
if ($LASTEXITCODE -ne 0) { throw 'pip download failed' }

Get-ChildItem $wheelDir -Filter *.whl | ForEach-Object {
    $extractDir = Join-Path $Destination ('unpacked\' + $_.BaseName)
    if (-not (Test-Path $extractDir)) {
        Expand-Wheel $_.FullName $extractDir
    }
}

$sdkRoot = Join-Path $Destination 'sdk'
foreach ($sub in 'include', 'bin', 'lib') {
    New-Item -ItemType Directory -Force (Join-Path $sdkRoot $sub) | Out-Null
}
Get-ChildItem (Join-Path $Destination 'unpacked') -Directory | ForEach-Object {
    $nvidiaRoot = Join-Path $_.FullName 'nvidia'
    Get-ChildItem $nvidiaRoot -Directory | ForEach-Object {
        foreach ($sub in 'include', 'bin') {
            $source = Join-Path $_.FullName $sub
            if (Test-Path $source) {
                Copy-Item -Path (Join-Path $source '*') -Destination (Join-Path $sdkRoot $sub) -Recurse -Force
            }
        }
    }
}
Write-Host "CUDA SDK subset ready at $sdkRoot"

if ($ProfilerTools) {
    $cuptiRoot = Join-Path $PSScriptRoot '..\.deps\cupti'
    $profilerWheels = Join-Path $cuptiRoot 'wheels'
    New-Item -ItemType Directory -Force $profilerWheels | Out-Null
    python -m pip download --no-deps --only-binary=:all: --platform win_amd64 "nvidia-cuda-cupti-cu$CudaMajor" "nvidia-cuda-nvcc-cu$CudaMajor" -d $profilerWheels
    if ($LASTEXITCODE -ne 0) { throw 'pip download of profiler tools failed' }
    Get-ChildItem $profilerWheels -Filter *.whl | ForEach-Object {
        $extractDir = Join-Path $cuptiRoot ('unpacked' + $_.BaseName)
        if (-not (Test-Path $extractDir)) {
            Expand-Wheel $_.FullName $extractDir
        }
    }
    foreach ($sub in 'include', 'bin') {
        New-Item -ItemType Directory -Force (Join-Path $cuptiRoot $sub) | Out-Null
    }
    Get-ChildItem (Join-Path $cuptiRoot 'unpacked') -Directory | ForEach-Object {
        $cupti = Join-Path $_.FullName 'nvidia\cuda_cupti'
        if (Test-Path $cupti) {
            Copy-Item -Path (Join-Path $cupti 'include\*') -Destination (Join-Path $cuptiRoot 'include') -Recurse -Force
            Copy-Item -Path (Join-Path $cupti 'bin\*') -Destination (Join-Path $cuptiRoot 'bin') -Recurse -Force
        }
        $crt = Join-Path $_.FullName 'nvidia\cuda_nvcc\include\crt'
        if (Test-Path $crt) {
            Copy-Item -Path $crt -Destination (Join-Path $cuptiRoot 'include') -Recurse -Force
        }
    }
    Write-Host "CUPTI ready at $cuptiRoot (balls_insncount needs GPU performance counters enabled for all users)"
}
