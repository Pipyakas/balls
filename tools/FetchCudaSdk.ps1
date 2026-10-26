param(
    [string]$Destination = (Join-Path $PSScriptRoot '..\.deps\cuda'),
    [string]$CudaMajor = '12'
)

$ErrorActionPreference = 'Stop'
$wheelDir = Join-Path $Destination 'wheels'
New-Item -ItemType Directory -Force $wheelDir | Out-Null

$packages = @("nvidia-cuda-nvrtc-cu$CudaMajor", "nvidia-cuda-runtime-cu$CudaMajor")
python -m pip download --no-deps --only-binary=:all: --platform win_amd64 @packages -d $wheelDir
if ($LASTEXITCODE -ne 0) { throw 'pip download failed' }

Get-ChildItem $wheelDir -Filter *.whl | ForEach-Object {
    $extractDir = Join-Path $Destination ('unpacked\' + $_.BaseName)
    if (-not (Test-Path $extractDir)) {
        Expand-Archive -Path $_.FullName -DestinationPath $extractDir
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
