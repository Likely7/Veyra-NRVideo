param([Parameter(Mandatory=$true)][string]$PlayerExe,[Parameter(Mandatory=$true)][string]$OutputDirectory)
# UI smoke for the combined PS5-control + capture-card mode and the refactored
# capture popup. Never contacts a console: it only verifies panel structure,
# checkbox expansion and the shared capture block embedding. Real PS5/capture
# connection is NOT tested here.
$ErrorActionPreference='Stop'
$PlayerExe=(Resolve-Path -LiteralPath $PlayerExe).Path
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class RpCombined {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,EnumProc cb,IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 public struct Rect {public int left,top,right,bottom;}
 public static string Text(IntPtr h){var s=new StringBuilder(1024);GetWindowText(h,s,1024);return s.ToString();}
 public static IntPtr Find(uint pid,string cls){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint owner;GetWindowThreadProcessId(h,out owner);var s=new StringBuilder(128);GetClassName(h,s,128);if(owner==pid&&s.ToString()==cls)found=h;return true;},IntPtr.Zero);return found;}
 public static IntPtr Button(IntPtr root,string text){IntPtr found=IntPtr.Zero;EnumChildWindows(root,(h,p)=>{if(Text(h)==text)found=h;return true;},IntPtr.Zero);return found;}
}
'@
$oldLog=$env:VEYRA_LOG_FILE
$process=$null
try {
 $env:VEYRA_LOG_FILE=Join-Path $OutputDirectory 'ui-combined-app.log'
 $process=Start-Process -FilePath $PlayerExe -ArgumentList @('--smoke-empty','--smoke-seconds','90','--no-nr','--no-sr','--no-fg') -PassThru -WindowStyle Hidden
 $deadline=[DateTime]::UtcNow.AddSeconds(8)
 do {$main=[RpCombined]::Find($process.Id,'VeyraApp');if($main -eq [IntPtr]::Zero){Start-Sleep -Milliseconds 50}}while($main -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
 if($main -eq [IntPtr]::Zero){throw 'Veyra main window did not open'}
 $button=[RpCombined]::Button($main,'PS5');if($button -eq [IntPtr]::Zero){throw 'PS5 entry missing'}
 $id=[RpCombined]::GetDlgCtrlID($button)
 [RpCombined]::PostMessage($main,0x111,[IntPtr]$id,[IntPtr]::Zero)|Out-Null
 $deadline=[DateTime]::UtcNow.AddSeconds(3)
 do {$panel=[RpCombined]::Find($process.Id,'VeyraRemotePlaySetup');if($panel -eq [IntPtr]::Zero){Start-Sleep -Milliseconds 25}}while($panel -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
 if($panel -eq [IntPtr]::Zero){throw 'Remote Play panel missing'}
 [RpCombined]::SendMessage($panel,0,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 $checkbox=[RpCombined]::GetDlgItem($panel,24)
 if($checkbox -eq [IntPtr]::Zero){throw 'UseCapture checkbox (id 24) missing'}
 $embedded=[RpCombined]::GetDlgItem($panel,601)
 if($embedded -eq [IntPtr]::Zero){throw 'embedded capture device combo (id 601) missing'}
 # Normalize machine state: a persisted UseCapture=1 from earlier manual use
 # must not fail the start-hidden assertion.
 if([RpCombined]::SendMessage($checkbox,0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 1){
   [RpCombined]::SendMessage($checkbox,0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
   Start-Sleep -Milliseconds 400
 }
 if([RpCombined]::IsWindowVisible($embedded)){throw 'embedded capture block must start hidden'}
 $before=New-Object RpCombined+Rect;[RpCombined]::GetWindowRect($panel,[ref]$before)|Out-Null
 [RpCombined]::SendMessage($checkbox,0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 Start-Sleep -Milliseconds 400
 $after=New-Object RpCombined+Rect;[RpCombined]::GetWindowRect($panel,[ref]$after)|Out-Null
 if(($after.right-$after.left)-($before.right-$before.left) -lt 100){throw 'panel did not widen for the right-hand capture column'}
 if(($after.bottom-$after.top)-($before.bottom-$before.top) -gt 16){throw 'combined mode must not grow the window height'}
 if(-not [RpCombined]::IsWindowVisible($embedded)){throw 'embedded capture device combo not visible after enabling'}
 $viewOnly=[RpCombined]::GetDlgItem($panel,17)
 if($viewOnly -ne [IntPtr]::Zero -and [RpCombined]::IsWindowEnabled($viewOnly)){throw 'view-only must be disabled in combined mode'}
 $quality=[RpCombined]::GetDlgItem($panel,4)
 if($quality -ne [IntPtr]::Zero -and [RpCombined]::IsWindowEnabled($quality)){throw 'stream quality combo must be disabled in combined mode'}
 for($control=601;$control -le 619;$control++){if([RpCombined]::GetDlgItem($panel,$control) -eq [IntPtr]::Zero -and ($control -in @(601,602,603,610,612,613,615,616,618))){throw "embedded capture control $control missing"}}
 [RpCombined]::SendMessage($checkbox,0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 Start-Sleep -Milliseconds 400
 $restored=New-Object RpCombined+Rect;[RpCombined]::GetWindowRect($panel,[ref]$restored)|Out-Null
 if([RpCombined]::IsWindowVisible($embedded)){throw 'embedded capture block still visible after unchecking'}
 if([Math]::Abs(($restored.right-$restored.left)-($before.right-$before.left)) -gt 16){throw 'panel width did not restore'}
 [RpCombined]::PostMessage($panel,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 Start-Sleep -Milliseconds 200
 # Refactored standalone capture popup must still build its shared block.
 $captureButton=[RpCombined]::Button($main,'采集');if($captureButton -eq [IntPtr]::Zero){throw 'capture entry button missing'}
 $captureId=[RpCombined]::GetDlgCtrlID($captureButton)
 [RpCombined]::PostMessage($main,0x111,[IntPtr]$captureId,[IntPtr]::Zero)|Out-Null
 $deadline=[DateTime]::UtcNow.AddSeconds(3)
 do {$popup=[RpCombined]::Find($process.Id,'VeyraCaptureSetup');if($popup -eq [IntPtr]::Zero){Start-Sleep -Milliseconds 25}}while($popup -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
 if($popup -eq [IntPtr]::Zero){throw 'capture popup missing'}
 [RpCombined]::SendMessage($popup,0,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 if([RpCombined]::GetDlgItem($popup,1) -eq [IntPtr]::Zero){throw 'capture popup device combo (id 1) missing'}
 if([RpCombined]::GetDlgItem($popup,4) -eq [IntPtr]::Zero){throw 'capture popup connect button (id 4) missing'}
 if([RpCombined]::GetDlgItem($popup,2) -eq [IntPtr]::Zero){throw 'capture popup format combo (id 2) missing'}
 [RpCombined]::PostMessage($popup,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 [RpCombined]::PostMessage($main,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 if(-not $process.WaitForExit(5000)){throw 'owned UI test did not exit'}
 Write-Output 'REMOTEPLAY_COMBINED_UI_PASS checkbox_toggle=1 embedded_block=1 viewonly_disabled=1 capture_popup_refactor=1 PS5_CONNECTION_NOT_TESTED=1 CAPTURE_DEVICE_NOT_TESTED=1'
} finally {
 $env:VEYRA_LOG_FILE=$oldLog
 if($process -and -not $process.HasExited){Stop-Process -Id $process.Id -Force}
}
