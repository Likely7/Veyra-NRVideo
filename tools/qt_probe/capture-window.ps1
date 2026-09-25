# Captures the app window even when it is occluded.
#
# Screen-region capture (CopyFromScreen) grabs whatever is on top, which is why
# earlier attempts produced the editor instead of the app: this session runs from a
# terminal that holds the foreground. PrintWindow with PW_RENDERFULLCONTENT asks the
# window to render itself, so the capture does not depend on z-order.
param(
  [string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app',
  [string]$Page = 'pro',
  [string]$Clip = '',
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\qml-shots',
  [string]$Name = '',
  [int]$WaitSeconds = 9
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;
public class PW {
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  delegate bool EnumProc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static IntPtr Largest(uint want) {
    IntPtr best = IntPtr.Zero; long area = 0;
    EnumWindows((h, p) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != want || !IsWindowVisible(h)) return true;
      RECT r; if (!GetWindowRect(h, out r)) return true;
      long a = (long)(r.Right - r.Left) * (r.Bottom - r.Top);
      if (a > area) { area = a; best = h; }
      return true;
    }, IntPtr.Zero);
    return best;
  }
}
"@
New-Item -ItemType Directory -Force $Out | Out-Null
$exe = Join-Path $App 'veyra_qml_ui.exe'
$launch = @('--page', $Page)
if ($Clip -ne '') { $launch += ('"' + $Clip + '"') }
$p = Start-Process -FilePath $exe -ArgumentList $launch -PassThru
Start-Sleep -Seconds $WaitSeconds
if ($p.HasExited) { "the app exited early"; exit 1 }

$h = [PW]::Largest([uint32]$p.Id)
if ($h -eq [IntPtr]::Zero) { "no visible window for pid $($p.Id)"; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 1 }
$r = New-Object PW+RECT
[PW]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top
if ($w -lt 400) { "window is only ${w}x${ht}; refusing to save" ; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 1 }

$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
# 2 = PW_RENDERFULLCONTENT, which is what makes this work for a window that is not
# in front and for surfaces rendered through a compositor.
[PW]::PrintWindow($h, $dc, 2) | Out-Null
$g.ReleaseHdc($dc)
$label = if ($Name -ne '') { $Name } else { $Page }
$file = Join-Path $Out ("qml-$label.png")
$bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $file  (${w}x${ht})"
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
