# Captures the app's largest top-level window.
#
# Get-Process's MainWindowHandle picked a taskbar preview (136x40) instead of the
# real window, so the window is chosen by enumerating every top-level window the
# process owns and taking the biggest one that is visible.
param(
  [string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app',
  [string]$Page = 'pro',
  [string]$Clip = '',
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\qml-shots',
  [string]$Name = '',
  [int]$WaitSeconds = 9
)
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;using System.Collections.Generic;using System.Runtime.InteropServices;
public class Cap {
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  delegate bool EnumProc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  public static void MouseClick() { mouse_event(0x0002, 0, 0, 0, IntPtr.Zero); mouse_event(0x0004, 0, 0, 0, IntPtr.Zero); }
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
if ($Clip -ne '') { $launch += $Clip }
$p = Start-Process -FilePath $exe -ArgumentList $launch -PassThru
Start-Sleep -Seconds $WaitSeconds
if ($p.HasExited) { "the app exited early"; exit 1 }


# The dock retracts, so it has to be summoned and then clicked while the pointer
# stays inside it. Offsets are measured from the dock's own layout.
function Switch-Page($win, $rect, $page) {
  $cx = [int](($rect.Left + $rect.Right) / 2)
  [Cap]::SetCursorPos($cx, $rect.Top + 60) | Out-Null
  Start-Sleep -Milliseconds 250
  [Cap]::SetCursorPos($cx, $rect.Top + 2) | Out-Null
  Start-Sleep -Milliseconds 1000
  # min, pro, exp, set as laid out left to right in the dock.
  $offset = @{ 'min' = -43; 'pro' = -9; 'exp' = 26; 'set' = 60 }[$page]
  if ($null -eq $offset) { return }
  [Cap]::SetCursorPos($cx + $offset, $rect.Top + 30) | Out-Null
  Start-Sleep -Milliseconds 300
  [Cap]::MouseClick() | Out-Null
  Start-Sleep -Milliseconds 1000
}

$h = [Cap]::Largest([uint32]$p.Id)
if ($h -eq [IntPtr]::Zero) { "no visible window for pid $($p.Id)"; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 1 }
[Cap]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 900
if ($Clip -ne '') { Switch-Page $h $null $Page }

$r = New-Object Cap+RECT
[Cap]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top
if ($w -lt 400) { "window is only ${w}x${ht}; refusing to save a preview" ; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 1 }
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$label = if ($Name -ne '') { $Name } else { $Page }
$file = Join-Path $Out ("qml-$label-" + (Get-Date -Format 'HHmmss') + ".png")
$bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $file  (${w}x${ht})"
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
