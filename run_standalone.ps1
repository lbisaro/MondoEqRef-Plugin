$ErrorActionPreference = "Stop"
$LogFile = "run_only.log"
Clear-Content $LogFile -ErrorAction SilentlyContinue

function Log {
    param([string]$Message)
    Write-Host $Message
    Add-Content -Path $LogFile -Value $Message
}

Log "========================================"
Log "Starting Application"
Log "========================================"

$ExePath = "build\MondoEqRefApp_artefacts\Debug\MondoEqRef Standalone.exe"

if (Test-Path $ExePath) {
    # Launch the process and wait for it to exit
    $process = Start-Process -FilePath $ExePath -PassThru -Wait -NoNewWindow
    Log "Application exited with code $($process.ExitCode)."
} else {
    Log "Executable not found at: $ExePath. Please build it first."
}
