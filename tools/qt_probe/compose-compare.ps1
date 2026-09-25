# Lays each design reference (left) next to the QML shot of the same state (right),
# labelled, for the frame-by-frame comparison. Missing sides are drawn as a grey box
# with the reason, not skipped, so a gap is visible in the set.
param(
  [string]$Design = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\design-ref',
  [string]$Qml = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\qml-ref',
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\compare',
  [string[]]$Frames = @()
)
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$ids = Get-ChildItem -LiteralPath $Design -Filter 'f-*.png' | ForEach-Object { $_.BaseName }
$font = New-Object Drawing.Font('Microsoft YaHei', 14)
$white = [Drawing.Brushes]::White
foreach ($id in $ids) {
  if ($Frames.Count -and $Frames -notcontains $id) { continue }
  $d = [Drawing.Image]::FromFile((Join-Path $Design "$id.png"))
  $qp = Join-Path $Qml "qml-$id.png"
  $q = if (Test-Path -LiteralPath $qp) { [Drawing.Image]::FromFile($qp) } else { $null }
  $qw = if ($q) { $q.Width } else { $d.Width }
  $qh = if ($q) { $q.Height } else { $d.Height }
  $gap = 16; $head = 32
  $bmp = New-Object Drawing.Bitmap ($d.Width + $gap + $qw), ($head + [Math]::Max($d.Height, $qh))
  $g = [Drawing.Graphics]::FromImage($bmp)
  $g.Clear([Drawing.Color]::FromArgb(40, 40, 46))
  $g.DrawString("$id  设计稿 $($d.Width)x$($d.Height)", $font, $white, 8, 4)
  $g.DrawString("QML $(if ($q) { "$($q.Width)x$($q.Height)" } else { '缺失' })", $font, $white, $d.Width + $gap + 8, 4)
  $g.DrawImage($d, 0, $head, $d.Width, $d.Height)
  if ($q) { $g.DrawImage($q, $d.Width + $gap, $head, $q.Width, $q.Height); $q.Dispose() }
  else { $g.FillRectangle([Drawing.Brushes]::DimGray, $d.Width + $gap, $head, $qw, $qh) }
  $file = Join-Path $Out "cmp-$id.png"
  $bmp.Save($file, [Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose(); $d.Dispose()
  "saved $file"
}
