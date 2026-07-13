param(
    [string]$Port,
    [string]$Nrfutil = ''
)

$ErrorActionPreference = 'Stop'

function Resolve-NrfutilPath {
    param([string]$RequestedPath)

    $candidates = @()

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        $candidates += $RequestedPath
    }

    $candidates += @(
        (Join-Path $PSScriptRoot 'nrfutil.exe'),
        'C:\nrfutil\nrfutil.exe'
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $pathCommands = @(Get-Command nrfutil.exe -All -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandType -eq 'Application' })

    $windowsDir = [System.IO.Path]::GetFullPath($env:WINDIR).TrimEnd('\')
    $fromPath = $pathCommands |
        Where-Object {
            $source = [System.IO.Path]::GetFullPath($_.Source)
            -not $source.StartsWith($windowsDir, [System.StringComparison]::OrdinalIgnoreCase)
        } |
        Select-Object -First 1

    if (-not $fromPath) {
        $fromPath = $pathCommands | Select-Object -First 1
    }

    if ($fromPath) {
        return $fromPath.Source
    }

    throw 'nrfutil.exe not found. Put it in C:\nrfutil\nrfutil.exe, next to this script, or add it to PATH.'
}

function Invoke-Nrfutil {
    param(
        [string]$Exe,
        [string[]]$Arguments
    )

    Write-Host ''
    Write-Host "> $Exe $($Arguments -join ' ')"
    & $Exe @Arguments

    if ($LASTEXITCODE -ne 0) {
        throw "nrfutil command failed with exit code $LASTEXITCODE."
    }
}

$nrfutilExe = Resolve-NrfutilPath -RequestedPath $Nrfutil

$nrfutilHome = 'C:\nrfutil\home'
$windowsDir = [System.IO.Path]::GetFullPath($env:WINDIR).TrimEnd('\')

if (Test-Path -LiteralPath $nrfutilHome) {
    $env:NRFUTIL_HOME = $nrfutilHome
} elseif (-not [string]::IsNullOrWhiteSpace($env:NRFUTIL_HOME)) {
    $resolvedHome = [System.IO.Path]::GetFullPath($env:NRFUTIL_HOME)
    if ($resolvedHome.StartsWith($windowsDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item Env:NRFUTIL_HOME -ErrorAction SilentlyContinue
    }
}

$dfuPackage = Join-Path $PSScriptRoot 'dfu_application.zip'
if (-not (Test-Path -LiteralPath $dfuPackage)) {
    throw "DFU package not found: $dfuPackage"
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    $Port = Read-Host 'Input DFU COM port, for example COM22'
}

$Port = $Port.Trim().ToUpperInvariant()
if ($Port -notmatch '^COM\d+$') {
    throw "Invalid COM port: $Port. Expected format: COM22"
}

Write-Host ''
Write-Host 'XIAO nRF54LM20B USB DFU'
Write-Host "nrfutil: $nrfutilExe"
if ([string]::IsNullOrWhiteSpace($env:NRFUTIL_HOME)) {
    Write-Host 'NRFUTIL_HOME: <nrfutil default>'
} else {
    Write-Host "NRFUTIL_HOME: $env:NRFUTIL_HOME"
}
Write-Host "Port: $Port"
Write-Host "DFU package: $dfuPackage"

Invoke-Nrfutil -Exe $nrfutilExe -Arguments @(
    'mcu-manager', 'serial', 'image-list',
    '--serial-port', $Port,
    '--timeout', '60'
)

Invoke-Nrfutil -Exe $nrfutilExe -Arguments @(
    'mcu-manager', 'serial', 'image-upload',
    '--serial-port', $Port,
    '--timeout', '60',
    '--firmware', $dfuPackage
)

try {
    Invoke-Nrfutil -Exe $nrfutilExe -Arguments @(
        'mcu-manager', 'serial', 'reset',
        '--serial-port', $Port,
        '--timeout', '60'
    )
} catch {
    Write-Warning "Upload finished, but reset command did not complete: $($_.Exception.Message)"
    Write-Warning 'If the board did not reset automatically, reset it manually.'
}

Write-Host ''
Write-Host 'DFU finished. Check that the red LED blinks every 500 ms after reset.'
