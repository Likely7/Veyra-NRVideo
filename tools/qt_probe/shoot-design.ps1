# Renders the approved design frames (prototypes/ui-redesign-2026-09-25/shot2.html?f=<id>)
# at their exact sizes with headless Edge. These are the references every QML screen is
# compared against. shotpage.js mounts one app per page and sets the title to 'ready'
# after 900 ms; the virtual time budget covers that and the dialog entrance animations.
param(
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\design-ref',
  [string[]]$Frames = @(),
  [int]$BudgetMs = 4000
)
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$proto = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'prototypes\ui-redesign-2026-09-25\shot2.html'
$sizes = [ordered]@{
  'f-home'='1280,800'; 'f-min'='1280,583'; 'f-min169'='1280,766'; 'f-cap'='1280,800'; 'f-ps5'='1280,800'
  'f-scr'='1280,800'; 'f-sub'='1280,800'; 'f-aud'='1280,800'; 'f-pro'='1280,800'; 'f-fg'='1280,800'
  'f-color'='1280,800'; 'f-save'='1280,800'; 'f-manage'='1280,800'; 'f-tonode'='1280,800'
  'f-node'='1600,1150'; 'f-exp'='1280,800'; 'f-set'='1280,800'
}
New-Item -ItemType Directory -Force $Out | Out-Null
$profile = 'E:\项目\Veyra\tmp\ui-qml-migration-20260925\edge-profile'
New-Item -ItemType Directory -Force $profile | Out-Null
$url0 = 'file:///' + ($proto -replace '\\', '/')
foreach ($id in $sizes.Keys) {
  if ($Frames.Count -and $Frames -notcontains $id) { continue }
  $png = Join-Path $Out "$id.png"
  $a = @('--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
         "--user-data-dir=`"$profile`"", "--window-size=$($sizes[$id])",
         "--virtual-time-budget=$BudgetMs", "--screenshot=`"$png`"", "`"$url0`?f=$id`"")
  $p = Start-Process -FilePath $edge -ArgumentList $a -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(60000)) { $p.Kill(); "$id timeout"; continue }
  if (Test-Path -LiteralPath $png) {
    Add-Type -AssemblyName System.Drawing
    $img = [Drawing.Image]::FromFile($png); "$id $($img.Width)x$($img.Height)"; $img.Dispose()
  } else { "$id missing" }
}
