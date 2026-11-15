# Drives the window of the application from PowerShell, to see a change working without computer-use.
# The coordinates are relative to the top left corner of the window (with its border), in physical pixels.
#
#   tools/ui.ps1 start <file> [log]     Starts build/Release/Shenmue.exe opening <file>; stdout goes to [log]
#   tools/ui.ps1 shot <png>             Saves a screenshot of the window to <png>
#   tools/ui.ps1 click <x> <y>          Left click
#   tools/ui.ps1 wheel <x> <y> [ticks]  Mouse wheel, down (negative ticks scroll up)
#   tools/ui.ps1 type <keys>            Sends keys with the syntax of SendKeys ("{LEFT}", "^s", "abc"...)
#   tools/ui.ps1 stop                   Stops the application
param([string]$Action, [string]$Arg1, [string]$Arg2, [string]$Arg3)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class ShenmueUi {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
# Without it the coordinates and the size of the window are scaled on high DPI screens
[ShenmueUi]::SetProcessDPIAware() | Out-Null

$root = Split-Path -Parent $PSScriptRoot
$exeFolder = Join-Path $root "build\Release"

function Get-Window {
    $p = Get-Process Shenmue -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($null -eq $p) { throw "Shenmue isn't running" }
    [ShenmueUi]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 300
    $r = New-Object ShenmueUi+RECT
    [ShenmueUi]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $r
}

function Move-Cursor([string]$x, [string]$y) {
    $r = Get-Window
    [ShenmueUi]::SetCursorPos($r.L + [int]$x, $r.T + [int]$y) | Out-Null
    Start-Sleep -Milliseconds 150
}

switch ($Action) {
    "start" {
        Get-Process Shenmue -ErrorAction SilentlyContinue | Stop-Process -Force
        $file = (Resolve-Path $Arg1).Path
        $options = @{ FilePath = (Join-Path $exeFolder "Shenmue.exe"); ArgumentList = "`"$file`""; WorkingDirectory = $exeFolder }
        if ($Arg2) { $options.RedirectStandardOutput = $Arg2 }
        Start-Process @options
        # Until the window exists and the file is loaded
        $deadline = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $deadline -and -not (Get-Process Shenmue -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 })) {
            Start-Sleep -Milliseconds 250
        }
        Start-Sleep -Seconds 3
    }
    "shot" {
        $r = Get-Window
        $bitmap = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($r.L, $r.T, 0, 0, $bitmap.Size)
        $bitmap.Save([System.IO.Path]::GetFullPath($Arg1))
        $graphics.Dispose(); $bitmap.Dispose()
        "Saved $Arg1 ($($r.R - $r.L)x$($r.B - $r.T) at $($r.L),$($r.T))"
    }
    "click" {
        Move-Cursor $Arg1 $Arg2
        [ShenmueUi]::mouse_event(2, 0, 0, 0, 0); Start-Sleep -Milliseconds 60; [ShenmueUi]::mouse_event(4, 0, 0, 0, 0)
        Start-Sleep -Milliseconds 700
    }
    "wheel" {
        $ticks = 1; if ($Arg3) { $ticks = [int]$Arg3 }
        Move-Cursor $Arg1 $Arg2
        [ShenmueUi]::mouse_event(0x800, 0, 0, -120 * $ticks, 0)
        Start-Sleep -Milliseconds 500
    }
    "type" {
        Add-Type -AssemblyName System.Windows.Forms
        Get-Window | Out-Null
        [System.Windows.Forms.SendKeys]::SendWait($Arg1)
        Start-Sleep -Milliseconds 700
    }
    "stop" { Get-Process Shenmue -ErrorAction SilentlyContinue | Stop-Process -Force }
    default { throw "Usage: ui.ps1 start|shot|click|wheel|type|stop [arguments]" }
}
