# Drags a file onto the app with a real OLE drag (the path Explorer uses): a small
# source form starts DoDragDrop while injected mouse moves carry it to the target
# point in the app window and release. Window-relative target point.
param(
  [string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app',
  [string]$Page = 'home',
  [Parameter(Mandatory)] [string]$File,
  [int]$X = 640, [int]$Y = 400,
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\drop',
  [int]$WaitSeconds = 6, [int]$AfterMs = 3000,
  # Move the pointer a little after the release, as a hand would.
  [switch]$Wiggle,
  [string]$AfterKeys = '',
  [string]$AfterPoint = '',
  [switch]$Kick,
  # Control run: everything except the drag, to check the harness itself.
  [switch]$NoDrag
)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;
public class DW {
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint f, uint t, out IntPtr r);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, int d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  delegate bool EnumProc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static IntPtr Largest(uint want) {
    IntPtr best = IntPtr.Zero; long area = 0;
    EnumWindows((h, p) => { uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != want || !IsWindowVisible(h)) return true; RECT r; if (!GetWindowRect(h, out r)) return true;
      long a = (long)(r.Right - r.Left) * (r.Bottom - r.Top); if (a > area) { area = a; best = h; } return true; }, IntPtr.Zero);
    return best;
  }
}
"@
New-Item -ItemType Directory -Force $Out | Out-Null
$p = Start-Process -FilePath (Join-Path $App 'veyra_qml_ui.exe') -ArgumentList @('--page', $Page) -PassThru
Start-Sleep -Seconds $WaitSeconds
$h = [DW]::Largest([uint32]$p.Id)
if ($h -eq [IntPtr]::Zero) { "no window"; Stop-Process -Id $p.Id -Force; exit 1 }
[DW]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0003) | Out-Null
$r = New-Object DW+RECT; [DW]::GetWindowRect($h, [ref]$r) | Out-Null
$tx = $r.Left + $X; $ty = $r.Top + $Y

$form = New-Object System.Windows.Forms.Form
$form.StartPosition = 'Manual'; $form.Location = New-Object System.Drawing.Point(($r.Right + 20), ($r.Top + 20))
$form.Size = New-Object System.Drawing.Size(160, 120); $form.TopMost = $true; $form.Text = 'drop source'
$sx = $form.Location.X + 80; $sy = $form.Location.Y + 60
# The pointer is moved by a second process: while DoDragDrop runs, this runspace is
# busy, so no timer script block here could fire.
$vw = [System.Windows.Forms.SystemInformation]::VirtualScreen
$mover = @"
Add-Type -Name M -Namespace N -MemberDefinition "[DllImport(""user32.dll"")] public static extern void mouse_event(uint f, int x, int y, int d, System.IntPtr e);"
function Abs(`$x, `$y) { [N.M]::mouse_event(0xC001, [int]((`$x - $($vw.Left)) * 65535 / $($vw.Width - 1)), [int]((`$y - $($vw.Top)) * 65535 / $($vw.Height - 1)), 0, [IntPtr]::Zero) }
Start-Sleep -Milliseconds 600
Abs $sx $sy; Start-Sleep -Milliseconds 150
[N.M]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 300
for (`$i = 1; `$i -le 20; `$i++) { Abs ($sx + ($tx - $sx) * `$i / 20) ($sy + ($ty - $sy) * `$i / 20); Start-Sleep -Milliseconds 40 }
Start-Sleep -Milliseconds 300
[N.M]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero)
if ($Wiggle) { Start-Sleep -Milliseconds 500; for (`$i = 1; `$i -le 10; `$i++) { Abs ($tx + `$i * 3) $ty; Start-Sleep -Milliseconds 50 } }
"@
$form.Add_Shown({
  $form.Activate()
  $moverFile = Join-Path $Out 'mover.ps1'
  Set-Content -LiteralPath $moverFile -Value $mover -Encoding UTF8
  Start-Process powershell -ArgumentList @('-NoProfile', '-File', $moverFile) -WindowStyle Hidden
})
# The mover presses the button over this form; the drag starts from that press.
$form.Add_MouseDown({
  $data = New-Object System.Windows.Forms.DataObject
  $list = New-Object System.Collections.Specialized.StringCollection; [void]$list.Add($File)
  $data.SetFileDropList($list)
  $t0 = Get-Date
  $res = $form.DoDragDrop($data, [System.Windows.Forms.DragDropEffects]::Copy)
  Set-Content -LiteralPath (Join-Path $Out 'drag-result.txt') -Value ("drag result: $res after {0:N0} ms" -f ((Get-Date) - $t0).TotalMilliseconds)
  $form.Close()
})
# The source keeps pumping messages while it waits, as Explorer does: the target
# may still call into the data object after the drop, and an STA thread that only
# sleeps leaves that cross-process COM call (and the app's GUI thread) blocked.
function Pump([int]$ms) { $end = (Get-Date).AddMilliseconds($ms); while ((Get-Date) -lt $end) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 15 } }
if (-not $NoDrag) { [System.Windows.Forms.Application]::Run($form) }
Pump $AfterMs
# Whether the app's GUI thread still answers after the drop.
$res = [IntPtr]::Zero
$ok = [DW]::SendMessageTimeoutW($h, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 1000, [ref]$res)
"gui thread responds=$($ok -ne [IntPtr]::Zero) hung=$([DW]::IsHungAppWindow($h))"
if ($Kick) { [DW]::PostMessageW($h, 0, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null; "posted WM_NULL"; Pump 1500 }
# Optional pointer position after the drop (window-relative "x,y"), e.g. the dock's
# hot zone, to see whether the app still reacts once a drop was delivered.
if ($AfterPoint -ne '') { $ax, $ay = $AfterPoint -split ','; [DW]::SetCursorPos($r.Left + [int]$ax, $r.Top + [int]$ay) | Out-Null; Pump 1500 }
# Optional keys after the drop ("17+69" is Ctrl+E), to see whether the app still
# handles input once a drop has been delivered.
if ($AfterKeys -ne '') {
  [DW]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 200
  $vks = $AfterKeys -split '\+' | ForEach-Object { [byte]$_ }
  foreach ($v in $vks) { [DW]::keybd_event($v, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 30 }
  [array]::Reverse($vks); foreach ($v in $vks) { [DW]::keybd_event($v, 0, 2, [IntPtr]::Zero) }
  Pump 1500
}
[DW]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $ht; $g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc(); [DW]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc)
$file = Join-Path $Out 'after-drop.png'; $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
"saved $file (${w}x${ht})"
# The same area read from the screen: PrintWindow and the composed desktop can
# disagree about a flip-model child window, so both are kept.
$bmp = New-Object System.Drawing.Bitmap $w, $ht; $g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$file = Join-Path $Out 'after-drop-screen.png'; $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
"saved $file"
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
