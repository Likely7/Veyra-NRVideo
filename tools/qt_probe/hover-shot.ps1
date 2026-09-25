# Pointer-driven review shot: launches the app (optionally with a clip), brings it to
# the front, walks the cursor through the given window-relative points and captures
# after each step. Hover state needs a real pointer, which the other shot scripts do
# not have; this is what exercises the dock's hot zone over a playing picture.
param(
  [string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app',
  [string]$Page = 'min',
  [string]$Clip = '',
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\hover',
  [string]$Extra = '',
  # "x,y,name[,waitMs]" per step, steps separated by ';', window-relative pixels. One
  # string: powershell -File does not split an array argument.
  [string]$Steps = '640,4,top',
  [int]$WaitSeconds = 8
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;
public class HW {
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
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
$launch = @('--page', $Page)
if ($Extra -ne '') { $launch += ($Extra -split '\s+' | Where-Object { $_ -ne '' }) }
if ($Clip -ne '') { $launch += ('"' + $Clip + '"') }
$p = Start-Process -FilePath (Join-Path $App 'veyra_qml_ui.exe') -ArgumentList $launch -PassThru
Start-Sleep -Seconds $WaitSeconds
if ($p.HasExited) { "the app exited early"; exit 1 }
$h = [HW]::Largest([uint32]$p.Id)
if ($h -eq [IntPtr]::Zero) { "no window"; Stop-Process -Id $p.Id -Force; exit 1 }
# Topmost for the duration, so the terminal cannot sit over the hot zone.
[HW]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0003) | Out-Null
[HW]::SetForegroundWindow($h) | Out-Null
$r = New-Object HW+RECT
foreach ($s in ($Steps -split ';')) {
  $x, $y, $name, $ms = $s -split ','
  if (-not $ms) { $ms = 900 }
  [HW]::GetWindowRect($h, [ref]$r) | Out-Null
  [HW]::SetCursorPos($r.Left + [int]$x, $r.Top + [int]$y) | Out-Null
  Start-Sleep -Milliseconds ([int]$ms)
  [HW]::GetWindowRect($h, [ref]$r) | Out-Null
  $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap $w, $ht
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc(); [HW]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc)
  $file = Join-Path $Out ("hover-$name.png")
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
  "saved $file (${w}x${ht})"
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
