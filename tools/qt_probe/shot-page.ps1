# Screenshots the QML app on a chosen page, so the result can be compared with
# the design reference for the same screen.
#
# The page is chosen by clicking the dock button at a known offset, because the
# app has no command-line page switch: adding one just for tests would be a
# feature the product does not need.
param(
  [string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app',
  [string]$Page = 'pro',
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\qml-shots',
  [string]$Clip = '',
  [int]$WaitSeconds = 7
)
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;using System.Runtime.InteropServices;
public class W2 {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
New-Item -ItemType Directory -Force $Out | Out-Null

$exe = Join-Path $App 'veyra_qml_ui.exe'
$argList = @()
if ($Clip -ne '') { $argList = @($Clip) }
$p = Start-Process -FilePath $exe -ArgumentList $argList -PassThru
Start-Sleep -Seconds $WaitSeconds
$p.Refresh()
if ($p.MainWindowHandle -eq 0) { "no window; the app may have exited"; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 1 }
$h = $p.MainWindowHandle
[W2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500

# The dock only appears when the pointer reaches the top edge, so it has to be
# summoned deliberately: the mouse is moved to the window centre first, then to
# the top edge, then the page button is clicked.
$r = New-Object W2+RECT
[W2]::GetWindowRect($h, [ref]$r) | Out-Null
$cx = [int](($r.Left + $r.Right) / 2)
[W2]::SetCursorPos($cx, $r.Top + 40) | Out-Null
Start-Sleep -Milliseconds 300
[W2]::SetCursorPos($cx, $r.Top + 2) | Out-Null
Start-Sleep -Milliseconds 900

# Dock buttons sit centred; the offsets below are measured from the dock's own
# layout (logo 40 + separators and 32px buttons with 2px gaps).
$dockCentre = $cx
$offsets = @{ 'min' = -60; 'pro' = 40; 'exp' = 140; 'set' = 240; 'home' = -160 }
if ($offsets.ContainsKey($Page)) {
  [W2]::SetCursorPos($dockCentre + $offsets[$Page], $r.Top + 30) | Out-Null
  Start-Sleep -Milliseconds 250
  [W2]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)  # left down
  [W2]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)  # left up
  Start-Sleep -Milliseconds 900
}
[W2]::SetCursorPos($r.Right + 300, $r.Bottom + 300) | Out-Null
Start-Sleep -Milliseconds 400
[W2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 200

[W2]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$file = Join-Path $Out ("qml-" + $Page + "-" + (Get-Date -Format 'HHmmss') + ".png")
$bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $file  (${w}x${ht})"
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
