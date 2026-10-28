param(
    [string]$BinaryPath = "build-wt/bin/Release/balls.exe",
    [string[]]$Renderers = @("nvrhi", "webgpu"),
    [string[]]$Apis = @("d3d12", "vulkan"),
    [string]$Gpu = "auto",
    [string]$Level = "DA_FirstLevel",
    [string]$Mode = "normal",
    [int]$WarmupSeconds = 2,
    [int]$DurationSeconds = 4,
    [int]$Port = 29992
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $BinaryPath)) {
    if (Test-Path "build/webgpu/bin/Release/balls.exe") {
        $BinaryPath = "build/webgpu/bin/Release/balls.exe"
    } elseif (Test-Path "build-wt/bin/Release/balls.exe") {
        $BinaryPath = "build-wt/bin/Release/balls.exe"
    } else {
        Write-Error "Binary not found at '$BinaryPath'."
        exit 1
    }
}

$results = @()

foreach ($renderer in $Renderers) {
    foreach ($api in $Apis) {
        Write-Host "=== Benchmarking: Renderer=$renderer, API=$api, GPU=$Gpu, Level=$Level ===" -ForegroundColor Cyan
        $launchArgs = @(
            "--renderer", $renderer,
            "--api", $api,
            "--gpu", $Gpu,
            "--play", $Mode, $Level,
            "--control-port", $Port
        )

        $process = Start-Process -FilePath $BinaryPath -ArgumentList $launchArgs -PassThru
        $samples = @()

        try {
            # Wait for control server to become responsive
            $connected = $false
            for ($i = 0; $i -lt 40; $i++) {
                Start-Sleep -Milliseconds 250
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
                Write-Warning "Could not connect to /state endpoint for $renderer / $api"
                continue
            }

            # Warmup period
            Start-Sleep -Seconds $WarmupSeconds

            # Sample metrics
            $endTime = (Get-Date).AddSeconds($DurationSeconds)
            while ((Get-Date) -lt $endTime) {
                Start-Sleep -Milliseconds 250
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
                    Renderer     = $renderer
                    API          = $api
                    Device       = $last.renderer
                    FPS          = $avgFps
                    MinFPS       = $last.minFps
                    "Frame(ms)"  = $avgFrameMs
                    "GPU(ms)"    = $avgGpuMs
                    "Present(ms)"= $avgPresentMs
                }
            }
        } finally {
            if (-not $process.HasExited) {
                try {
                    Invoke-RestMethod -Uri "http://127.0.0.1:$Port/quit" -TimeoutSec 1 -ErrorAction SilentlyContinue | Out-Null
                    Start-Sleep -Milliseconds 300
                } catch {}
                if (-not $process.HasExited) {
                    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                }
            }
        }
    }
}

Write-Host "`n=== Comparison Summary ===" -ForegroundColor Green
$results | Format-Table -AutoSize
return $results
