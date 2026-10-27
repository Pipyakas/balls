param(
    [string]$BinaryPath = "build/webgpu/bin/Release/ColorBlastBalls.exe",
    [string]$Api = "d3d12",
    [string]$Gpu = "auto",
    [int]$DurationSeconds = 5,
    [int]$Port = 29991
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $BinaryPath)) {
    Write-Error "Binary not found at '$BinaryPath'. Please build ColorBlastBalls first."
    exit 1
}

$results = @()
$renderers = @("nvrhi", "webgpu")

foreach ($renderer in $renderers) {
    Write-Host "=== Benchmarking Renderer: $renderer (API: $Api, GPU: $Gpu) ===" -ForegroundColor Cyan
    $args = @(
        "--renderer", $renderer,
        "--api", $Api,
        "--gpu", $Gpu,
        "--hidden",
        "--control-port", $Port
    )

    $process = Start-Process -FilePath $BinaryPath -ArgumentList $args -PassThru
    $samples = @()

    try {
        # Wait for control server to become responsive
        $connected = $false
        for ($i = 0; $i -lt 30; $i++) {
            Start-Sleep -Milliseconds 200
            if ($process.HasExited) {
                break
            }
            try {
                $response = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/state" -TimeoutSec 1
                if ($response.ok) {
                    $connected = $true
                    break
                }
            } catch {
                # Server not ready yet
            }
        }

        if (-not $connected) {
            Write-Warning "Could not connect to /state endpoint for renderer $renderer"
            continue
        }

        # Sample metrics
        $endTime = (Get-Date).AddSeconds($DurationSeconds)
        while ((Get-Date) -lt $endTime) {
            Start-Sleep -Milliseconds 500
            if ($process.HasExited) {
                break
            }
            try {
                $state = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/state" -TimeoutSec 1
                if ($state.ok) {
                    $samples += $state
                }
            } catch {
            }
        }

        if ($samples.Count -gt 0) {
            $last = $samples[-1]
            $avgFps = [Math]::Round(($samples | Measure-Object -Property fps -Average).Average, 1)
            $avgFrameMs = [Math]::Round(($samples | Measure-Object -Property frameMs -Average).Average, 3)
            $avgGpuMs = [Math]::Round(($samples | Measure-Object -Property gpuMs -Average).Average, 3)
            $avgPresentMs = [Math]::Round(($samples | Measure-Object -Property presentMs -Average).Average, 3)

            $results += [PSCustomObject]@{
                Renderer    = $renderer
                Device      = $last.renderer
                FPS         = $avgFps
                MinFPS      = $last.minFps
                "Frame(ms)" = $avgFrameMs
                "GPU(ms)"   = $avgGpuMs
                "Present(ms)"= $avgPresentMs
            }
        }
    } finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

Write-Host "`n=== Comparison Summary ===" -ForegroundColor Green
$results | Format-Table -AutoSize
