# Motion sampling (G0.5): the same motion on the design and in the QML app, as curves.
#
# Design: shotpage.js ?motion=1&probe=<name> pauses every Web Animation and seeks it in
# 10 ms steps, reading the animated value back (--dump-dom picks up the CSV).
# QML: --motion-probe <name> starts the motion and logs one value per rendered frame.
# Output per probe: design-<name>.csv, qml-<name>.csv and chart-<name>.png (design in
# blue, QML in orange) under $Out.
param(
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\motion',
  [string[]]$Probes = @('dock', 'switch', 'page'),
  [string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app'
)
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$proto = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'prototypes\ui-redesign-2026-09-25\shot2.html'
$profile = 'E:\项目\Veyra\tmp\ui-qml-migration-20260925\edge-profile'
$log = Join-Path $App 'logs\veyra-qml.log'
# Where each motion lives in the design: the switch sits on the capture dialog.
$designFrame = @{ dock = 'f-home'; switch = 'f-cap'; page = 'f-home'; seg = 'f-cap' }

function Read-Curve([string[]]$lines) {
  # "t,value" or "t,opacity,y": the curve is the last column.
  $lines | Where-Object { $_ -match '^\d' } | ForEach-Object {
    $c = $_ -split ','; [pscustomobject]@{ t = [double]$c[0]; v = [double]$c[-1] }
  }
}

foreach ($name in $Probes) {
  $url = "file:///" + $proto.Replace([char]92, [char]47) + "?f=$($designFrame[$name])&motion=1&probe=$name"
  $dom = & $edge --headless=new --disable-gpu "--user-data-dir=$profile" --window-size=1280,800 --virtual-time-budget=3000 --dump-dom $url 2>$null | Out-String
  $m = [regex]::Match($dom, '<pre id="motion-csv">([^<]*)</pre>')
  $design = if ($m.Success) { $m.Groups[1].Value -split "`n" } else { @('error no design curve') }
  $design | Set-Content -Encoding utf8 (Join-Path $Out "design-$name.csv")

  $before = if (Test-Path $log) { (Get-Content $log).Count } else { 0 }
  $p = Start-Process -FilePath (Join-Path $App 'veyra_qml_ui.exe') -ArgumentList @('--page', 'home', '--motion-probe', $name) -PassThru
  Start-Sleep -Seconds 4
  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 500
  $qml = @('t_ms,value') + (Get-Content $log | Select-Object -Skip $before | ForEach-Object {
    if ($_ -match "motion-probe,$name,(.*)$") { $Matches[1] } })
  $qml | Set-Content -Encoding utf8 (Join-Path $Out "qml-$name.csv")

  $a = @(Read-Curve $design); $b = @(Read-Curve $qml)
  $all = @($a.v) + @($b.v)
  if ($all.Count -lt 2) { "$name  no data (design $($a.Count), qml $($b.Count))"; continue }
  $lo = ($all | Measure-Object -Minimum).Minimum; $hi = ($all | Measure-Object -Maximum).Maximum
  if ($hi - $lo -lt 1e-6) { $hi = $lo + 1 }
  $W = 900; $H = 420; $pad = 50
  $bmp = New-Object Drawing.Bitmap $W, $H; $g = [Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = 'AntiAlias'; $g.Clear([Drawing.Color]::FromArgb(24, 24, 28))
  $font = New-Object Drawing.Font('Microsoft YaHei', 11)
  $px = { param($t) $pad + ($W - 2 * $pad) * $t / 1000 }
  $py = { param($v) $H - $pad - ($H - 2 * $pad) * ($v - $lo) / ($hi - $lo) }
  $grid = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(60, 60, 70))
  foreach ($t in 0, 200, 400, 600, 800, 1000) { $x = & $px $t; $g.DrawLine($grid, $x, $pad, $x, $H - $pad); $g.DrawString("$t", $font, [Drawing.Brushes]::Gray, $x - 12, $H - $pad + 6) }
  foreach ($curve in @(@{ d = $a; c = [Drawing.Color]::FromArgb(110, 168, 254) }, @{ d = $b; c = [Drawing.Color]::FromArgb(255, 140, 50) })) {
    $pen = New-Object Drawing.Pen ($curve.c, 2)
    for ($i = 1; $i -lt $curve.d.Count; $i++) {
      $g.DrawLine($pen, (& $px $curve.d[$i - 1].t), (& $py $curve.d[$i - 1].v), (& $px $curve.d[$i].t), (& $py $curve.d[$i].v))
    }
  }
  $dMax = ($a.v | Measure-Object -Maximum).Maximum; $qMax = ($b.v | Measure-Object -Maximum).Maximum
  $g.DrawString("$name   蓝=设计稿 (峰值 $([math]::Round($dMax,2)))   橙=QML (峰值 $([math]::Round($qMax,2)))   横轴 ms", $font, [Drawing.Brushes]::White, $pad, 14)
  $png = Join-Path $Out "chart-$name.png"; $bmp.Save($png, [Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
  "$name  design $($a.Count) samples peak $dMax   qml $($b.Count) samples peak $qMax   -> $png"
}
