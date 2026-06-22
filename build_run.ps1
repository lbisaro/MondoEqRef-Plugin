$ErrorActionPreference = "Stop"
$LogFile = "build_and_run.log"
Clear-Content $LogFile -ErrorAction SilentlyContinue

function Log {
    param([string]$Message)
    Write-Host $Message
    Add-Content -Path $LogFile -Value $Message
}

Log "========================================"
Log "Starting Build Process"
Log "========================================"

cmake --build build --config Debug -j4 | Tee-Object -FilePath $LogFile -Append

if ($LASTEXITCODE -ne 0) {
    Log "========================================"
    Log "Build FAILED!"
    Log "========================================"
    exit $LASTEXITCODE
}

Log "========================================"
Log "Build SUCCEEDED! Launching application..."
Log "========================================"

$ExePath = "build\MondoEqRefApp_artefacts\Debug\MondoEqRef Standalone.exe"

if (Test-Path $ExePath) {
    $process = Start-Process -FilePath $ExePath -PassThru -Wait -NoNewWindow
    Log "Application exited with code $($process.ExitCode)."
} else {
    Log "Executable not found at: $ExePath"
}
