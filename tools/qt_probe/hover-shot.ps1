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
  # "x,y,name[,waitMs[,action]]" per step, steps separated by ';', window-relative pixels.
  # action: click (a left click at the point), or keys "k<vk>[+<vk>..]" tapped in order with
  # modifiers held ("k17+76" is Ctrl+L), or "kd<vk>" / "ku<vk>" to press or release one key. One
  # string: powershell -File does not split an array argument.
  [string]$Steps = '640,4,top',
  [int]$WaitSeconds = 8,
  # Also paint the process's other top-level windows (the cinema pill window, G3.2)
  # over the main one at their screen offsets. PrintWindow renders one HWND only.
  [switch]$Compose
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
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, int d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowRgnBox(IntPtr h, out RECT r);
  delegate bool EnumProc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static System.Collections.Generic.List<IntPtr> All(uint want) {
    var list = new System.Collections.Generic.List<IntPtr>();
    EnumWindows((h, p) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == want && IsWindowVisible(h)) list.Add(h);
      return true;
    }, IntPtr.Zero);
    return list;   // top of the z-order first
  }
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
  $x, $y, $name, $ms, $click = $s -split ','
  if (-not $ms) { $ms = 900 }
  [HW]::GetWindowRect($h, [ref]$r) | Out-Null
  [HW]::SetCursorPos($r.Left + [int]$x, $r.Top + [int]$y) | Out-Null
  if ($click -eq 'click') { Start-Sleep -Milliseconds 150; [HW]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); [HW]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero) }
  elseif ($click -match '^kd(\d+)$') { [HW]::keybd_event([byte]$Matches[1], 0, 0, [IntPtr]::Zero) }
  elseif ($click -match '^ku(\d+)$') { [HW]::keybd_event([byte]$Matches[1], 0, 2, [IntPtr]::Zero) }
  elseif ($click -match '^k([\d+]+)$') {
    $vks = $Matches[1] -split '\+' | ForEach-Object { [byte]$_ }
    foreach ($v in $vks) { [HW]::keybd_event($v, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 30 }
    [array]::Reverse($vks); foreach ($v in $vks) { [HW]::keybd_event($v, 0, 2, [IntPtr]::Zero) }
  }
  Start-Sleep -Milliseconds ([int]$ms)
  [HW]::GetWindowRect($h, [ref]$r) | Out-Null
  $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap $w, $ht
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc(); [HW]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc)
  if ($Compose) {
    $others = [HW]::All([uint32]$p.Id); $others.Reverse()
    foreach ($o in $others) {
      if ($o -eq $h) { continue }
      $orr = New-Object HW+RECT; [HW]::GetWindowRect($o, [ref]$orr) | Out-Null
      $ow = $orr.Right - $orr.Left; $oh = $orr.Bottom - $orr.Top
      if ($ow -le 0 -or $oh -le 0) { continue }
      $ob = New-Object System.Drawing.Bitmap $ow, $oh, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
      $og = [System.Drawing.Graphics]::FromImage($ob); $og.Clear([System.Drawing.Color]::Transparent)
      $odc = $og.GetHdc(); [HW]::PrintWindow($o, $odc, 2) | Out-Null; $og.ReleaseHdc($odc); $og.Dispose()
      # A masked window (setMask -> SetWindowRgn) shows only its region on screen;
      # PrintWindow paints the rest black, so only the region's box is composed.
      $box = New-Object HW+RECT
      $kind = [HW]::GetWindowRgnBox($o, [ref]$box)
      if ($kind -le 1) { $box.Left = 0; $box.Top = 0; $box.Right = $ow; $box.Bottom = $oh }
      $src = New-Object System.Drawing.Rectangle $box.Left, $box.Top, ($box.Right - $box.Left), ($box.Bottom - $box.Top)
      $g.DrawImage($ob, ($orr.Left - $r.Left + $box.Left), ($orr.Top - $r.Top + $box.Top), $src, [System.Drawing.GraphicsUnit]::Pixel)
      $ob.Save((Join-Path $Out ("hover-$name-win-$($o.ToInt64()).png")), [System.Drawing.Imaging.ImageFormat]::Png); $ob.Dispose()
      "  composed window $($o.ToInt64()) ${ow}x${oh} at $($orr.Left - $r.Left),$($orr.Top - $r.Top)"
    }
  }
  $file = Join-Path $Out ("hover-$name.png")
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
  "saved $file (${w}x${ht})"
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
