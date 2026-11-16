# Drives a hidden instance of the application from PowerShell, to see a change working in the background: the window is
# never shown, and the input goes to the application through its control pipe, not through the mouse and the keyboard of
# the desktop. Each action returns when the application has done it, with what it printed or "ok"; it fails if the
# application reports an error. The coordinates are pixels of the screenshots, from the top left corner of the window
# contents. Time advances only with the actions, 1/60 s per frame drawn, so the same actions give the same images.
#
#   tools/ui.ps1 start [file] [log]     Starts build/Release/Shenmue.exe hidden, opening [file]; its output goes to
#                                       [log] (stdout) and [log].err (stderr), by default in %TEMP%\Shenmue-ui.log
#   tools/ui.ps1 shot <png>             Draws a frame and saves it to <png>
#   tools/ui.ps1 click <x> <y> [right|middle]
#   tools/ui.ps1 wheel <x> <y> [ticks]  Mouse wheel, down (negative ticks scroll up)
#   tools/ui.ps1 type <text>            Types the text in the text box being edited
#   tools/ui.ps1 key <keys>             A key with its modifiers: "ctrl+s", "left", "escape", "f"...
#   tools/ui.ps1 do <command>           Any other command of the headless mode (src/app/headless.h): "move 10 20",
#                                       "drag 900 400 1100 400", "hold w 30", "frames 60", "status", "open <file>",
#                                       "dialog <path>"...
#   tools/ui.ps1 stop                   Ends the instance started by start (and only it)
param([string]$Action, [string]$Arg1, [string]$Arg2, [string]$Arg3)

$root = Split-Path -Parent $PSScriptRoot
$exeFolder = Join-Path $root "build\Release"
$pipeName = "Shenmue-ui"
$pidFile = Join-Path $env:TEMP "Shenmue-ui.pid"

function Get-FullPath([string]$path) {
    if ([System.IO.Path]::IsPathRooted($path)) { return $path }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $path))
}

# Sends a command and returns the reply. Fails if the application can't be reached or reports an error
function Send-Command([string]$command, [int]$timeout = 10000) {
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(".", $pipeName, [System.IO.Pipes.PipeDirection]::InOut)
    try {
        try { $pipe.Connect($timeout) } catch { throw "Shenmue isn't running: tools/ui.ps1 start" }
        $utf8 = New-Object System.Text.UTF8Encoding $false
        $writer = New-Object System.IO.StreamWriter($pipe, $utf8)
        $writer.Write("$command`n")
        $writer.Flush()
        $reader = New-Object System.IO.StreamReader($pipe, $utf8)
        $reply = $reader.ReadToEnd().TrimEnd()
    }
    finally { $pipe.Dispose() }
    if ($reply -match "(?m)^error:") { throw $reply }
    $reply
}

function Stop-Instance {
    if (-not (Test-Path $pidFile)) { return }
    $id = [int](Get-Content $pidFile)
    $process = Get-Process -Id $id -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -eq "Shenmue" }
    if ($null -ne $process) {
        try { Send-Command "quit" 2000 | Out-Null } catch { }
        if (-not $process.WaitForExit(5000)) { Stop-Process -Id $id -Force }
    }
    Remove-Item $pidFile
}

switch ($Action) {
    "start" {
        Stop-Instance
        $log = Join-Path $env:TEMP "Shenmue-ui.log"
        if ($Arg2) { $log = Get-FullPath $Arg2 }
        $arguments = "--control $pipeName"
        if ($Arg1) { $arguments += " `"$(Get-FullPath $Arg1)`"" }
        $process = Start-Process -FilePath (Join-Path $exeFolder "Shenmue.exe") -ArgumentList $arguments -WorkingDirectory $exeFolder `
            -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru
        Set-Content -Path $pidFile -Value $process.Id
        # The first command is answered once the file is loaded
        Send-Command "status" 60000
    }
    "shot" { Send-Command "shot $(Get-FullPath $Arg1)" }
    "click" { Send-Command ("click $Arg1 $Arg2 $Arg3".TrimEnd()) }
    "wheel" {
        $ticks = 1; if ($Arg3) { $ticks = [int]$Arg3 }
        Send-Command "wheel $Arg1 $Arg2 $ticks"
    }
    "type" { Send-Command "type $Arg1" }
    "key" { Send-Command "key $Arg1" }
    "do" { Send-Command (@($Arg1, $Arg2, $Arg3) -join " ").TrimEnd() }
    "stop" { Stop-Instance }
    default { throw "Usage: ui.ps1 start|shot|click|wheel|type|key|do|stop [arguments]" }
}
