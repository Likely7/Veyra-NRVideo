# Screenshot the running QML app so the interface can be looked at, not assumed.
param([string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app',
      [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\qml-shots',
      [int]$WaitSeconds = 7)
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
New-Item -ItemType Directory -Force $Out | Out-Null
$exe = Join-Path $App 'veyra_qml_ui.exe'
$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds $WaitSeconds
$p.Refresh()
if ($p.MainWindowHandle -eq 0) { "no main window handle; the app may have exited"; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 1 }
$h = $p.MainWindowHandle
[W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700
$r = New-Object W+RECT
[W]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$file = Join-Path $Out ("qml-" + (Get-Date -Format 'HHmmss') + ".png")
$bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $file  (${w}x${ht})"
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
