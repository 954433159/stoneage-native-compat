param(
    [ValidateSet('start', 'stop', 'status')][string]$Action = 'start',
    [switch]$NoClient
)
$ErrorActionPreference = 'Stop'
$instanceRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$linuxRoot = & wsl.exe -d Ubuntu --exec wslpath -u $instanceRoot.Replace('\', '/')
if ($LASTEXITCODE -ne 0 -or -not $linuxRoot) { throw 'Cannot resolve this instance in WSL Ubuntu.' }
$linuxRoot = $linuxRoot.Trim()
if (-not $linuxRoot.StartsWith('/')) { throw 'Unexpected WSL instance path.' }

function Invoke-Instance([string]$Operation) {
    & wsl.exe -d Ubuntu --exec python3 ($linuxRoot + '/tools/runtime.py') $Operation
    if ($LASTEXITCODE -ne 0) { throw ('Local server operation failed: ' + $Operation) }
}

function Get-OwnClient {
    $pidPath = Join-Path $instanceRoot 'runtime\client.pid'
    if (-not (Test-Path -LiteralPath $pidPath)) { return $null }
    $clientPid = [int](Get-Content -LiteralPath $pidPath)
    $clientProcess = Get-Process -Id $clientPid -ErrorAction SilentlyContinue
    if (-not $clientProcess) { return $null }
    if ($clientProcess.Path -ne (Join-Path $instanceRoot 'client\stoneage.exe')) {
        throw 'Recorded client PID belongs to another process; refusing to operate it.'
    }
    return $clientProcess
}

if ($Action -eq 'status') {
    Invoke-Instance 'status'
    $ownClient = Get-OwnClient
    if ($ownClient) { Write-Output ('Local client running: ' + $ownClient.Id) }
    else { Write-Output 'Local client is stopped.' }
    exit 0
}
if ($Action -eq 'stop') {
    $ownClient = Get-OwnClient
    if ($ownClient) {
        [void]$ownClient.CloseMainWindow()
        if (-not $ownClient.WaitForExit(10000)) {
            throw 'Close the local game client normally before stopping. No forced termination was used.'
        }
    }
    Invoke-Instance 'stop'
    exit 0
}

foreach ($required in @('build\gmsv', 'build\saac', 'runtime\settings.json', 'client\stoneage.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $instanceRoot $required))) { throw ('Missing prepared instance file: ' + $required) }
}
Invoke-Instance 'database'
Invoke-Instance 'native'
Invoke-Instance 'http'
Write-Output 'Waiting for native map and NPC initialization...'
Invoke-Instance 'ready'
if ($NoClient) { exit 0 }
$ownClient = Get-OwnClient
if ($ownClient) { Write-Output ('Local client already running: ' + $ownClient.Id); exit 0 }
$previousCompatibility = $env:__COMPAT_LAYER
try {
    $env:__COMPAT_LAYER = 'RunAsInvoker'
    $ownClient = Start-Process -FilePath (Join-Path $instanceRoot 'client\stoneage.exe') -WorkingDirectory (Join-Path $instanceRoot 'client') -WindowStyle Normal -PassThru
    [System.IO.File]::WriteAllText((Join-Path $instanceRoot 'runtime\client.pid'), [string]$ownClient.Id)
    Write-Output ('Local client started: ' + $ownClient.Id)
} finally {
    $env:__COMPAT_LAYER = $previousCompatibility
}
