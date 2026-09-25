# Shoots the QML app in the state of each of the 17 design frames (board.js FRAMES),
# no clip loaded, reduced motion on, so each shot is the settled final state and can
# be laid next to the design reference from shoot-design.ps1.
#
# With a clip the app jumps to the minimal page and the native video window covers the
# QML layer, so dialogs and the dock would not be visible; the design frames have no
# film either (the minimal page is black until a source plays).
param(
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\qml-ref',
  [string[]]$Frames = @(),
  [int]$WaitSeconds = 5
)
# Dialog keys differ: the design says savePresetList / managePresetsList / toNode, the
# QML DialogHost says save / manage. 'tonode' has no QML dialog yet (G4).
# Named $states, not $frames: PowerShell names are case-insensitive and it would
# overwrite the -Frames parameter.
$states = [ordered]@{
  'f-home'   = @('home', '')
  'f-min'    = @('min',  '--aspect 2.39')
  'f-min169' = @('min',  '--aspect 1.7778 --dock-pinned')
  'f-cap'    = @('home', '--dialog capture')
  'f-ps5'    = @('home', '--dialog ps5')
  'f-scr'    = @('home', '--dialog screen')
  'f-sub'    = @('min',  '--dialog subtitle')
  'f-aud'    = @('min',  '--dialog audio')
  'f-pro'    = @('pro',  '')
  'f-fg'     = @('pro',  '--tab fg')
  'f-color'  = @('pro',  '--tab color')
  'f-save'   = @('pro',  '--dialog save')
  'f-manage' = @('pro',  '--dialog manage')
  'f-tonode' = @('pro',  '--dialog tonode')
  'f-node'   = @('node', '--size 1600x1150')
  'f-exp'    = @('exp',  '')
  'f-set'    = @('set',  '')
  # Not design frames: states the frames only show in passing (menus), shot for review.
  'x-menu-src'    = @('pro', '--menu source')
  'x-menu-preset' = @('pro', '--menu preset')
}
$capture = Join-Path $PSScriptRoot 'capture-window.ps1'
foreach ($id in $states.Keys) {
  if ($Frames.Count -and $Frames -notcontains $id) { continue }
  $page, $extra = $states[$id]
  & $capture -Page $page -Out $Out -Name $id -Extra ("$extra --reduced-motion").Trim() -WaitSeconds $WaitSeconds
}
