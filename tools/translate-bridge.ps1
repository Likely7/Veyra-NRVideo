param([int]$Port = 17881)

# Reads Windows Live Captions through UI Automation, translates the text and
# draws a bilingual overlay above the Veyra video area. No compiler required:
# Windows PowerShell 5.1 + WPF + UIAutomationClient.
$ErrorActionPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = Split-Path -Parent $scriptDir
$logFile = Join-Path $rootDir 'logs\translate-bridge.log'

function Write-Log([string]$m) {
    try {
        $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
        Add-Content -Path $logFile -Value $line -Encoding UTF8
    } catch { }
}

Write-Log "start port=$Port pid=$PID"

# One sidecar only. The panel launches the bridge whenever its first /state
# handshake is slow, and a second instance brought a second subtitle window and
# a second HTTP listener: the panel then polled the old process while the new
# one drew on top of it, which is what "the subtitle hung" looked like.
# The mutex is released when the process exits, so a crash leaves nothing behind.
$script:instanceMutex = New-Object System.Threading.Mutex($false, 'Global\VeyraTranslateBridge')
if (-not $script:instanceMutex.WaitOne(0)) {
    Write-Log 'another sidecar is already running, exit'
    exit 0
}

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, PresentationFramework, PresentationCore, WindowsBase, System.Xaml
if (-not ('VyNative' -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class VyNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X; public int Y; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    // QML reports the picture's viewport in client coordinates; the client origin
    // turns it into screen pixels.
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT p);
    // The plate is owned by the Veyra window, so Windows keeps it over the
    // picture by itself; no foreground polling is needed any more.
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] public static extern int SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    // Sockets are inherited by Start-Process children (llama-server, captions);
    // a dead sidecar would then keep the port half-open forever.
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetHandleInformation(IntPtr h, uint mask, uint flags);
}
"@
}

# Live Captions rebuilds its caption pane when the audio device changes (a game
# switching station, a new capture card). The cached UI Automation node then
# keeps answering with the sentence it had when the pane died, so the subtitle
# froze until the caption loop noticed the text had stopped moving. The hook
# below turns that rebuild into an immediate signal instead of a 2.5 s wait.
if (-not ('VyHook' -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public delegate void VyWinEventDelegate(IntPtr hWinEventHook, uint eventType, IntPtr hwnd,
    int idObject, int idChild, uint dwEventThread, uint dwmsEventTime);
public static class VyHook {
    [DllImport("user32.dll")] public static extern IntPtr SetWinEventHook(uint min, uint max,
        IntPtr hmod, VyWinEventDelegate cb, uint pid, uint tid, uint flags);
    [DllImport("user32.dll")] public static extern bool UnhookWinEvent(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    // 1 while the caption pane has been rebuilt and not re-read yet.
    public static volatile int dirty = 0;
    public static int targetPid = 0;
    static VyWinEventDelegate cb;
    static IntPtr handle = IntPtr.Zero;
    // OUTOFCONTEXT | SKIPOWNPROCESS: the callback runs on the thread that
    // installed the hook, which is the WPF dispatcher (a pumped message loop).
    public static void Install(uint pid) {
        if (handle != IntPtr.Zero) { UnhookWinEvent(handle); handle = IntPtr.Zero; }
        cb = new VyWinEventDelegate(OnEvent);
        handle = SetWinEventHook(0x8000, 0x8001, IntPtr.Zero, cb, pid, 0, 0x0002);
    }
    static void OnEvent(IntPtr h, uint ev, IntPtr hwnd, int obj, int child, uint th, uint t) {
        if (hwnd == IntPtr.Zero) return;
        uint pid = 0;
        GetWindowThreadProcessId(hwnd, out pid);
        if (targetPid != 0 && (int)pid == targetPid) dirty = 1;
    }
}
"@
}

# Per-monitor v2 awareness, before any window exists: without it Windows bitmap
# stretches the overlay and the subtitle text turns blurry at 125 % scaling.
try { [void][VyNative]::SetProcessDpiAwarenessContext([IntPtr](-4)) } catch { }

$state = [hashtable]::Synchronized(@{
    stop       = $false
    raw        = ''
    dst        = ''
    seen       = ''
    done       = ''
    seenAt     = (Get-Date)
    asrOk      = $false
    message    = 'starting'
    engine     = 'mymemory'
    srcLang    = 'en-US'
    dstLang    = 'zh-CN'
    mode       = 'both'
    fontSize   = 26
    inset      = 96
    # Every look option starts off, matching the panel's factory defaults: a
    # fresh install shows plain text until the user turns a style on.
    backdrop   = $false
    stroke     = $false
    strokeW    = 1.4
    shadow     = $false
    shadowD    = 2.4
    # Fade a new line in instead of swapping it instantly.
    anim       = $false
    posV       = 'bottom'
    posH       = 'center'
    videoX     = 0
    videoY     = 0
    videoW     = 0
    videoH     = 0
    videoGiven = $false
    apiBase    = ''
    apiKey     = ''
    model      = ''
    modelState = 'off'
    # Translation is done in its own runspace: a slow or hanging API would
    # otherwise freeze the caption loop for the whole request timeout, which is
    # what "the subtitle is stuck" looked like. The caption loop only publishes
    # a request here; the worker fills dst and marks it done.
    req        = ''
    reqText    = ''
    reqId      = 0
    doneId     = 0
    failedId   = -1
    # One request at a time: a remote model answers slower than the captions
    # update, so the caption loop must not overwrite the request that is being
    # translated (that made every answer arrive stale and get dropped).
    busy       = $false
    busyAt     = (Get-Date)
    # Last error string, shown by the panel when a translation fails (a wrong
    # key or model name was invisible otherwise).
    err        = ''
    # Live Captions process id: the shell hook below only listens to it.
    capPid     = 0
    # Delta mode: when the caption only grew (same sentence, more words), the
    # part that was already translated is kept and only the new tail is sent.
    delta      = $true
    reqDelta   = $false
    reqBase    = ''
    reqLine    = ''
    doneLine   = ''
    doneDst    = ''
    # Stream the model answer: the first tokens reach the overlay while the rest
    # is still generating, instead of one visible jump at the end.
    stream     = $true
    # Last time caption text was seen. Kept in the shared state so the debug
    # hook can keep an injected sample alive like a real caption would.
    textAt     = (Get-Date)
    # Reference document (markdown) injected into the API prompt.
    docPath    = ''
    docName    = ''
    docChars   = 0
    docText    = ''
    docLoad    = ''
    docClear   = $false
    pickRequest = $false
    cfgSig     = ''
    llmPid     = 0
    lcSpawned  = $false
    lcHwnd     = 0
    lcLeft     = 0
    lcTop      = 0
})

# API settings and the reference document cannot go into the engine's preference
# table (it rejects keys it does not know), so the sidecar keeps them in its own
# file and the panel reads them back from /state at startup.
$cfgFile = Join-Path $rootDir 'runtime_local\translate-config.json'

function Get-CfgSig {
    return ("{0}|{1}|{2}|{3}|{4}" -f $state.engine, $state.apiBase, $state.apiKey, $state.model, $state.docPath)
}

function Save-Cfg {
    try {
        $o = [ordered]@{
            engine  = [string]$state.engine
            apiBase = [string]$state.apiBase
            apiKey  = [string]$state.apiKey
            model   = [string]$state.model
            docPath = [string]$state.docPath
        }
        $o | ConvertTo-Json -Depth 3 | Set-Content -Path $cfgFile -Encoding UTF8
        $state.cfgSig = Get-CfgSig
    } catch {
        Write-Log ('config write failed: ' + $_.Exception.Message)
    }
}

function Read-DocFile([string]$p) {
    if ([string]::IsNullOrWhiteSpace($p) -or -not (Test-Path -LiteralPath $p)) {
        $state.docPath = ''
        $state.docName = ''
        $state.docChars = 0
        $state.docText = ''
        return
    }
    try {
        $t = Get-Content -LiteralPath $p -Raw -Encoding UTF8
        if ($null -eq $t) { $t = '' }
        $t = ([string]$t).Trim()
        # Small local models have a short context; 8k characters is already more
        # than a caption prompt can use.
        if ($t.Length -gt 8000) { $t = $t.Substring(0, 8000) }
        $state.docPath = $p
        $state.docName = [IO.Path]::GetFileName($p)
        $state.docChars = $t.Length
        $state.docText = $t
        Write-Log ('reference doc loaded: ' + $state.docName + ' chars=' + $t.Length)
    } catch {
        Write-Log ('reference doc read failed: ' + $_.Exception.Message)
    }
}

if (Test-Path -LiteralPath $cfgFile) {
    try {
        $cfg = Get-Content -LiteralPath $cfgFile -Raw -Encoding UTF8 | ConvertFrom-Json
        # The engine choice is kept here too: the engine's preference table drops
        # translateEngine, so without this the panel fell back to MyMemory on
        # every restart and the custom endpoint was never used.
        if ($cfg.engine) { $state.engine = [string]$cfg.engine }
        if ($cfg.apiBase) { $state.apiBase = [string]$cfg.apiBase }
        if ($cfg.apiKey) { $state.apiKey = [string]$cfg.apiKey }
        if ($cfg.model) { $state.model = [string]$cfg.model }
        if ($cfg.docPath) { $state.docPath = [string]$cfg.docPath }
        $state.cfgSig = Get-CfgSig
    } catch {
        Write-Log 'config read failed'
    }
}
if ($state.docPath) { Read-DocFile $state.docPath }

$httpCode = @'
param($state, $Port, $cfgFile)

function Send-Json($stream, $obj) {
    $json = $obj | ConvertTo-Json -Compress -Depth 4
    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
    $head = "HTTP/1.1 200 OK`r`nContent-Type: application/json; charset=utf-8`r`nContent-Length: $($bytes.Length)`r`nConnection: close`r`n`r`n"
    $hb = [Text.Encoding]::ASCII.GetBytes($head)
    $stream.Write($hb, 0, $hb.Length)
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()
}

function Save-Cfg {
    try {
        $o = [ordered]@{
            engine  = [string]$state.engine
            apiBase = [string]$state.apiBase
            apiKey  = [string]$state.apiKey
            model   = [string]$state.model
            docPath = [string]$state.docPath
        }
        $o | ConvertTo-Json -Depth 3 | Set-Content -Path $cfgFile -Encoding UTF8
        $state.cfgSig = ("{0}|{1}|{2}|{3}|{4}" -f $state.engine, $state.apiBase, $state.apiKey, $state.model, $state.docPath)
    } catch { }
}

function Convert-TargetName([string]$code) {
    switch ($code) {
        'zh-CN' { return 'Simplified Chinese' }
        'zh-TW' { return 'Traditional Chinese' }
        'en'    { return 'English' }
        'ja'    { return 'Japanese' }
        'ko'    { return 'Korean' }
        default { return $code }
    }
}

# One-shot probe for the panel's test button. Runs in the control runspace, so a
# slow endpoint never blocks the captions; the answer carries the upstream error
# body so a bad key or model name is visible instead of a generic failure.
function Invoke-ApiProbe([string]$text) {
    if (-not $state.apiBase) { return @{ ok = $false; error = 'no base url' } }
    $model = if ($state.model) { $state.model } else { 'gpt-4o-mini' }
    $target = Convert-TargetName ([string]$state.dstLang)
    $sys = "Translate the user's live subtitle into $target. Reply with the translation only."
    if ($state.docText) {
        $sys = $sys + "`n`nReference material. Use its spellings and terminology:`n" + $state.docText
    }
    $body = @{
        model       = $model
        temperature = 0.2
        messages    = @(
            @{ role = 'system'; content = $sys },
            @{ role = 'user'; content = $text }
        )
    } | ConvertTo-Json -Depth 6 -Compress
    $headers = @{ 'Content-Type' = 'application/json' }
    if ($state.apiKey) { $headers['Authorization'] = "Bearer $($state.apiKey)" }
    $url = ([string]$state.apiBase).TrimEnd('/') + '/chat/completions'
    try {
        $r = Invoke-RestMethod -Uri $url -Method Post -Headers $headers `
            -Body ([Text.Encoding]::UTF8.GetBytes($body)) -TimeoutSec 20
        $out = ([string]$r.choices[0].message.content).Trim()
        if ([string]::IsNullOrWhiteSpace($out)) { return @{ ok = $false; error = 'empty reply' } }
        return @{ ok = $true; out = $out; model = $model }
    } catch {
        $msg = $_.Exception.Message
        $detail = ''
        try {
            $resp = $_.Exception.Response
            if ($resp) {
                $sr = New-Object IO.StreamReader($resp.GetResponseStream())
                $detail = $sr.ReadToEnd()
                $sr.Close()
            }
        } catch { }
        $all = ($msg + ' ' + $detail).Trim()
        if ($all.Length -gt 300) { $all = $all.Substring(0, 300) }
        return @{ ok = $false; error = $all }
    }
}

$listener = $null
for ($i = 0; $i -lt 10; $i++) {
    try {
        $listener = New-Object System.Net.Sockets.TcpListener -ArgumentList ([System.Net.IPAddress]::Loopback), $Port
        $listener.Start()
        break
    } catch {
        $listener = $null
        Start-Sleep -Milliseconds 400
    }
}
if ($null -eq $listener) {
    # Never keep running without a control port: the QML panel could not reach
    # this copy and a second subtitle window would be drawn on top of the first.
    $state.message = 'port busy'
    $state.stop = $true
    return
}
# Child processes must not inherit the listening socket, otherwise a dead
# sidecar leaves the port held open by llama-server and every later attempt to
# bind (and every panel request) hangs.
try { [void][VyNative]::SetHandleInformation($listener.Server.Handle, 1, 0) } catch { }

while (-not $state.stop) {
    try {
        if (-not $listener.Pending()) { Start-Sleep -Milliseconds 100; continue }
        $client = $listener.AcceptTcpClient()
    } catch { Start-Sleep -Milliseconds 100; continue }

    $client.ReceiveTimeout = 2000
    $stream = $client.GetStream()
    $buffer = New-Object byte[] 8192
    $read = 0
    try { $read = $stream.Read($buffer, 0, $buffer.Length) } catch { }

    if ($read -gt 0) {
        $req = [Text.Encoding]::ASCII.GetString($buffer, 0, $read)
        $first = ($req -split "`r`n")[0]
        $parts = $first -split ' '
        $path = if ($parts.Length -gt 1) { $parts[1] } else { '/' }

        if ($path -like '/config*') {
            $q = ''
            $i = $path.IndexOf('?')
            if ($i -ge 0) { $q = $path.Substring($i + 1) }
            foreach ($pair in $q.Split('&')) {
                if ([string]::IsNullOrEmpty($pair)) { continue }
                $kv = $pair.Split('=')
                if ($kv.Length -lt 2) { continue }
                $k = [Uri]::UnescapeDataString($kv[0])
                $v = [Uri]::UnescapeDataString($kv[1])
                switch ($k) {
                    'engine'   { $state.engine = $v }
                    'src'      { $state.srcLang = $v }
                    'dst'      { $state.dstLang = $v }
                    'mode'     { $state.mode = $v }
                    'size'     { $state.fontSize = [int]$v }
                    'inset'    { $state.inset = [int]$v }
                    'backdrop' { $state.backdrop = ($v -eq '1') }
                    'stroke'   { $state.stroke = ($v -eq '1') }
                    'strokeW'  { $state.strokeW = [double]$v }
                    'shadow'   { $state.shadow = ($v -eq '1') }
                    'shadowD'  { $state.shadowD = [double]$v }
                    'anim'     { $state.anim = ($v -eq '1') }
                    'stream'   { $state.stream = ($v -eq '1') }
                    'delta'    { $state.delta = ($v -eq '1') }
                    'vpos'     { $state.posV = $v }
                    'hpos'     { $state.posH = $v }
                    'vx'       { $state.videoX = [int]$v; $state.videoGiven = $true }
                    'vy'       { $state.videoY = [int]$v; $state.videoGiven = $true }
                    'vw'       { $state.videoW = [int]$v; $state.videoGiven = $true }
                    'vh'       { $state.videoH = [int]$v; $state.videoGiven = $true }
                    'apiBase'  { if ($state.apiBase -ne $v) { $state.apiBase = $v } }
                    'apiKey'   { if ($state.apiKey -ne $v) { $state.apiKey = $v } }
                    'model'    { if ($state.model -ne $v) { $state.model = $v } }
                    'doc'      { if ($v) { $state.docLoad = $v } }
                    'docClear' { if ($v -eq '1') { $state.docClear = $true } }
                }
            }
            # Only written when something really changed: the panel re-pushes its
            # whole configuration on window resizes and slider moves.
            $sig = ("{0}|{1}|{2}|{3}|{4}" -f $state.engine, $state.apiBase, $state.apiKey, $state.model, $state.docPath)
            if ($sig -ne $state.cfgSig) { Save-Cfg }
            Send-Json $stream @{ ok = $true }
        } elseif ($path -like '/pickdoc*') {
            # The file picker is a WPF dialog, so it can only be opened on the
            # overlay's dispatcher thread; this just raises the request.
            $state.pickRequest = $true
            Send-Json $stream @{ ok = $true }
        } elseif ($path -like '/apitest*') {
            $t = 'The quick brown fox jumps over the lazy dog.'
            $i = $path.IndexOf('?')
            if ($i -ge 0 -and $path.Length -gt ($i + 1)) {
                $t = [Uri]::UnescapeDataString($path.Substring($i + 1))
            }
            Send-Json $stream (Invoke-ApiProbe $t)
        } elseif ($path -like '/stop*') {
            $state.stop = $true
            Send-Json $stream @{ ok = $true }
        } elseif ($path -like '/test*') {
            # Debug hook: draws a sample pair so the overlay can be checked
            # without audio. Never used by the panel. The text goes through the
            # same request slot the captions use, so the translation worker and
            # the stale-answer guard are exercised as well.
            $t = 'sample subtitle'
            $i = $path.IndexOf('?')
            if ($i -ge 0) { $t = [Uri]::UnescapeDataString($path.Substring($i + 1)) }
            $state.raw = $t
            $state.textAt = Get-Date
            $state.busy = $true
            $state.busyAt = Get-Date
            # Same delta rule as the caption loop, so the debug hook exercises
            # exactly the path a growing caption takes.
            $base = [string]$state.doneDst
            $prev = [string]$state.doneLine
            $tail = ''
            if ([bool]$state.delta -and $base.Length -gt 0 -and $prev.Length -ge 6 -and
                    $t.Length -gt $prev.Length -and $t.StartsWith($prev, [StringComparison]::OrdinalIgnoreCase)) {
                $x = $t.Substring($prev.Length).Trim()
                if ($x.Length -ge 2 -and $x.Length -le 120) { $tail = $x }
            }
            $state.reqLine = $t
            $state.reqDelta = ($tail.Length -gt 0)
            $state.reqBase = if ($tail.Length -gt 0) { $base } else { '' }
            $state.req = if ($tail.Length -gt 0) { $tail } else { $t }
            $state.reqText = $t
            $state.reqId = [int]$state.reqId + 1
            Send-Json $stream @{ ok = $true; text = $t; delta = [bool]$state.reqDelta; sent = [string]$state.req }
        } elseif ($path -like '/state*') {
            Send-Json $stream ([ordered]@{
                up         = $true
                asr        = [bool]$state.asrOk
                message    = $state.message
                original   = $state.raw
                translated = $state.dst
                engine     = $state.engine
                model      = $state.modelState
                # API settings live in the sidecar's own file, so the panel reads
                # them back here. The key itself is never sent out; only whether
                # one is stored.
                apiBase    = [string]$state.apiBase
                apiModel   = [string]$state.model
                apiKeySet  = ([string]$state.apiKey).Length -gt 0
                doc        = [string]$state.docName
                docChars   = [int]$state.docChars
                # Reason of the last failed translation; empty once one succeeds.
                err        = [string]$state.err
            })
        } else {
            Send-Json $stream @{ ok = $true }
        }
    }

    try { $client.Close() } catch { }
}
try { $listener.Stop() } catch { }
'@

$asrCode = @'
param($state, $Port, $rootDir)

$llmPort = 8090
$llmExe = Join-Path $rootDir 'tools\llm\llama-server.exe'
$llmModel = Join-Path $rootDir 'tools\llm\Hy-MT2-1.8B-StreamRevise-v4-Q4_K_M.gguf'
$script:llmProc = $null
$script:llmWarm = $false
$script:llmStartAt = $null
$script:llmCheckedAt = Get-Date

function Log([string]$m) {
    try {
        Add-Content -Path (Join-Path $rootDir 'logs\translate-bridge.log') `
            -Value ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) -Encoding UTF8
    } catch { }
}

function Test-LocalServer {
    try {
        $r = Invoke-WebRequest -Uri "http://127.0.0.1:$llmPort/health" -UseBasicParsing -TimeoutSec 2
        return ($r.StatusCode -eq 200)
    } catch { return $false }
}

# Starts llama-server with the bundled GGUF. It never waits for the model: this
# function is called from the caption loop, and blocking there until the weights
# are in VRAM used to look like a frozen panel (no subtitle, no status change)
# for up to two minutes. The server is spawned once and its /health is polled on
# later ticks, while the original caption keeps flowing.
function Ensure-LocalServer {
    # A failed start is not retried on every tick; switching engines resets it.
    if ($state.modelState -eq 'failed') { return $false }
    if ($state.modelState -eq 'ready') {
        # Called on every caption tick: /health is only re-checked every few
        # seconds so the loop stays cheap.
        if (((Get-Date) - $script:llmCheckedAt).TotalSeconds -lt 3) { return $true }
        $script:llmCheckedAt = Get-Date
        if (Test-LocalServer) { return $true }
        # The server died under us; fall through and start it again.
        $script:llmWarm = $false
        $state.modelState = 'loading'
        $script:llmStartAt = Get-Date
    }
    if (-not (Test-Path $llmExe) -or -not (Test-Path $llmModel)) {
        $state.message = 'local model missing'
        $state.modelState = 'missing'
        return $false
    }
    if ($null -eq $script:llmProc -or $script:llmProc.HasExited) {
        Get-Process llama-server -ErrorAction SilentlyContinue | Stop-Process -Force
        $out = Join-Path $rootDir 'logs\llama-server.log'
        $err = Join-Path $rootDir 'logs\llama-server.err.log'
        $llmArgs = @('-m', ('"' + $llmModel + '"'), '--port', "$llmPort",
                     '-ngl', '99', '-c', '2048', '--no-webui', '-t', '6')
        $script:llmProc = Start-Process -FilePath $llmExe -ArgumentList $llmArgs `
            -RedirectStandardOutput $out -RedirectStandardError $err `
            -WindowStyle Hidden -PassThru
        $state.llmPid = $script:llmProc.Id
        $script:llmStartAt = Get-Date
        $state.modelState = 'loading'
        $state.message = 'loading local model'
        Log ("llama-server started pid=" + $script:llmProc.Id)
        return $false
    }
    if (Test-LocalServer) {
        $script:llmWarm = $true
        $script:llmCheckedAt = Get-Date
        $state.modelState = 'ready'
        Log ("llama-server ready in " + [int](((Get-Date) - $script:llmStartAt).TotalSeconds * 1000) + " ms")
        return $true
    }
    if ($null -eq $script:llmStartAt) { $script:llmStartAt = Get-Date }
    if (((Get-Date) - $script:llmStartAt).TotalSeconds -gt 120) {
        $state.message = 'local model failed'
        $state.modelState = 'failed'
        Log 'llama-server did not become ready'
        return $false
    }
    # Still loading: let the captions keep scrolling in the meantime.
    $state.modelState = 'loading'
    return $false
}


# Frees the VRAM as soon as the panel switches back to a cloud engine.
function Stop-LocalServer {
    if ($null -ne $script:llmProc -and -not $script:llmProc.HasExited) {
        try { $script:llmProc.Kill() } catch { }
        Log 'llama-server stopped'
    }
    $script:llmProc = $null
    $script:llmWarm = $false
    $script:llmStartAt = $null
    $script:llmCheckedAt = Get-Date
    $state.llmPid = 0
    $state.modelState = 'off'
}

function Find-LiveCaptionsWindow {
    $p = Get-Process LiveCaptions -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $p) { return $null }
    # The shell hook watches this process only; it is installed off this id.
    $state.capPid = [int]$p.Id
    $cond = New-Object System.Windows.Automation.PropertyCondition -ArgumentList `
        ([System.Windows.Automation.AutomationElement]::ProcessIdProperty), $p.Id
    return [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
        [System.Windows.Automation.TreeScope]::Children, $cond)
}

function Find-CaptionText($win) {
    if ($null -eq $win) { return $null }
    $cond = New-Object System.Windows.Automation.PropertyCondition -ArgumentList `
        ([System.Windows.Automation.AutomationElement]::AutomationIdProperty), 'CaptionsTextBlock'
    return $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
}

# Translation itself runs in its own runspace ($trCode below): doing it here
# made the caption loop wait for the network, and a slow endpoint then left the
# previous subtitle frozen on screen for the whole request timeout.

$script:lcLaunchAt = Get-Date

if (-not (Get-Process LiveCaptions -ErrorAction SilentlyContinue)) {
    Start-Process 'LiveCaptions'
    $state.lcSpawned = $true
    Start-Sleep -Milliseconds 1200
}

# Live Captions is only the ASR source. Its window is parked far off screen so
# the subtitle overlay stays the only window left over the video.
function Park-CaptionWindow($win) {
    $h = [IntPtr]::Zero
    try { $h = [IntPtr]$win.Current.NativeWindowHandle } catch { }
    if ($h -eq [IntPtr]::Zero) { return }
    $r = New-Object VyNative+RECT
    if (-not [VyNative]::GetWindowRect($h, [ref]$r)) { return }
    if ($r.Left -lt -10000) { return }
    if ($state.lcLeft -eq 0 -and $state.lcTop -eq 0) {
        $state.lcLeft = $r.Left
        $state.lcTop = $r.Top
    }
    # SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
    [void][VyNative]::SetWindowPos($h, [IntPtr]::Zero, -32000, -32000, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010)
    $state.lcHwnd = [int64]$h
    Log 'caption window parked'
}

# Leaves the desktop as it was: a caption window this sidecar started is closed,
# one the user already had open is moved back where it was.
function Restore-CaptionWindow {
    if ($state.lcSpawned) {
        Get-Process LiveCaptions -ErrorAction SilentlyContinue | Stop-Process -Force
        return
    }
    if ($state.lcHwnd -ne 0) {
        [void][VyNative]::SetWindowPos([IntPtr]$state.lcHwnd, [IntPtr]::Zero, [int]$state.lcLeft, [int]$state.lcTop, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010)
    }
}

# Only the newest caption line is shown. Live Captions keeps the finished
# sentence above the one being spoken, and translating both made the previous
# subtitle sit under the new one for seconds after the speaker had moved on.
function Clip-Text([string]$txt) {
    $lines = @($txt -split "`r?`n" | Where-Object { $_.Trim().Length -gt 0 })
    if ($lines.Count -eq 0) { return '' }
    $orig = [string]$lines[$lines.Count - 1]
    if ($orig.Length -gt 240) { $orig = $orig.Substring($orig.Length - 240) }
    return $orig.Trim()
}

# Live Captions keeps re-punctuating and re-capitalising the line it is still
# working on, so a plain string comparison cannot tell a rewrite of the current
# sentence from the start of the next one. Only the leading words matter here:
# a rewrite shares them, a new sentence does not.
function Same-Sentence([string]$a, [string]$b) {
    $na = ($a -replace '[^\w\s]', ' ').Trim().ToLowerInvariant()
    $nb = ($b -replace '[^\w\s]', ' ').Trim().ToLowerInvariant()
    if ($na.Length -eq 0 -or $nb.Length -eq 0) { return $false }
    $n = [Math]::Min($na.Length, $nb.Length)
    $i = 0
    while ($i -lt $n -and $na[$i] -eq $nb[$i]) { $i++ }
    return (($i -eq $n) -or ($i -ge 8))
}

$el = $null
$script:elAt = Get-Date
$script:lastTranslateAt = (Get-Date).AddSeconds(-5)
# Last string the caption element returned and when it last changed: a caption
# that stops moving is the only signal that the element went dead.
$script:lastTxt = ''
$script:txtAt = Get-Date

# Live Captions drops its text element entirely after a pause, so the loop can
# stop seeing any text without ever seeing an empty string. Both cases have to
# clear the old line, otherwise the last subtitle stays on screen forever, which
# is exactly what "the subtitle is stuck" looked like.
function Clear-Caption {
    $state.raw = ''
    $state.dst = ''
    $state.seen = ''
    $state.done = ''
    # Delta bookkeeping belongs to the cleared sentence: keeping it would make
    # the next caption append itself to text that is no longer on screen.
    $state.doneLine = ''
    $state.doneDst = ''
    $state.reqLine = ''
    $state.reqDelta = $false
    $state.reqBase = ''
    # Invalidate whatever the worker is still translating: its result would
    # otherwise put the cleared line back on screen.
    $state.req = ''
    $state.reqText = ''
    $state.reqId = [int]$state.reqId + 1
    $state.failedId = -1
}

function Test-CaptionStale {
    return ($state.raw.Length -gt 0 -and ((Get-Date) - [datetime]$state.textAt).TotalSeconds -gt 3)
}

while (-not $state.stop) {
    # Local engine: the model is started (not waited for) here so the captions
    # keep flowing while the weights load; a cloud engine releases the VRAM.
    if ($state.engine -eq 'local') {
        [void](Ensure-LocalServer)
    } elseif ($script:llmProc) {
        Stop-LocalServer
    }

    $win = Find-LiveCaptionsWindow
    if ($null -eq $win) {
        $state.asrOk = $false
        $state.message = 'starting captions'
        # Closing the caption window used to freeze the subtitles; bring it back
        # instead, it is invisible once parked.
        if (((Get-Date) - $script:lcLaunchAt).TotalSeconds -gt 6) {
            Start-Process 'LiveCaptions'
            $state.lcSpawned = $true
            $script:lcLaunchAt = Get-Date
        }
        Start-Sleep -Milliseconds 900
        continue
    }
    Park-CaptionWindow $win
    # The caption pane was just rebuilt (audio device switched): drop the cached
    # node now and re-resolve it on the next pass, instead of reading a node that
    # still answers with the old sentence.
    if ([VyHook]::dirty -ne 0) {
        [VyHook]::dirty = 0
        $el = $null
        $script:elAt = Get-Date
        $script:txtAt = Get-Date
        Log 'caption pane rebuilt'
        Start-Sleep -Milliseconds 80
        continue
    }
    # Live Captions rebuilds its caption pane when its language or settings
    # change; a cached element then reads a dead node and the text never moves
    # again. Re-resolving it every few seconds costs nothing and rules that out.
    if ($null -eq $el -or ((Get-Date) - $script:elAt).TotalSeconds -gt 20) {
        $el = Find-CaptionText $win
        $script:elAt = Get-Date
    }
    if ($null -eq $el) {
        $state.asrOk = $false
        $state.message = 'waiting for caption text'
        if (Test-CaptionStale) { Clear-Caption }
        Start-Sleep -Milliseconds 800
        continue
    }

    try {
        $txt = [string]$el.Current.Name
    } catch {
        $el = $null
        Start-Sleep -Milliseconds 400
        continue
    }

    # Switching the audio source (a game changing station, a new capture device)
    # makes Live Captions replace its caption pane. The cached element then keeps
    # returning the sentence it had when the pane died - still non-empty, so no
    # other check noticed - and the subtitle stayed frozen until the 20 s
    # re-resolve above. Watch the text instead of the clock: a caption that has
    # not moved for a couple of seconds is re-read from the tree, and a live pane
    # simply answers with the same line.
    if ($txt -ne $script:lastTxt) {
        $script:lastTxt = $txt
        $script:txtAt = Get-Date
    } elseif (((Get-Date) - $script:txtAt).TotalSeconds -gt 2.5) {
        $el = $null
        $script:txtAt = Get-Date
        Start-Sleep -Milliseconds 120
        continue
    }

    if ([string]::IsNullOrWhiteSpace($txt)) {
        $state.asrOk = $true
        $state.message = 'listening for audio'
        # Live Captions blanks its text after a pause; once it has been empty for
        # a moment the old sentence is cleared too instead of staying on screen.
        if (Test-CaptionStale) { Clear-Caption }
        Start-Sleep -Milliseconds 300
        continue
    }

    $state.asrOk = $true
    $state.textAt = Get-Date
    # A failing request keeps its status until a fresh one is published: showing
    # "running" over "translate failed" hid every bad key and bad model name.
    if ([int]$state.failedId -ne [int]$state.reqId -and $state.message -ne 'running') {
        $state.message = 'running'
    }

    # Streaming: the caption keeps growing while the speaker talks, so instead of
    # waiting for it to settle we re-translate the current text on a fixed cadence
    # and let the translated line revise itself as the sentence unfolds.
    $gap = if ($state.engine -eq 'local') { 350 } else { 900 }

    if ($txt -ne $state.seen) {
        $state.seen = $txt
        $state.seenAt = Get-Date
        $raw = Clip-Text $txt
        # A new line replaces the old one instead of extending it, so it is
        # translated on the very next tick. The previous translation is NOT
        # blanked out here: doing that made the Chinese line disappear and then
        # flash back a few hundred milliseconds later for every sentence. It is
        # left on screen and simply overwritten when the new text arrives.
        if ($raw -ne $state.raw -and -not (Same-Sentence $raw ([string]$state.raw))) {
            $script:lastTranslateAt = (Get-Date).AddSeconds(-5)
        }
        # The original line follows the live captions immediately; the translated
        # line catches up a moment later.
        $state.raw = $raw
    }

    # One request at a time. A remote model answers slower than the caption
    # updates, so publishing on every tick let reqId advance while a request was
    # in flight: every answer came back "stale" and was thrown away, which looked
    # like Chinese frozen on an old line while English kept scrolling.
    # A hung request must not stop the translation for the rest of the film. The
    # local model answers in well under a second, so waiting 30 s for it only left
    # the Chinese line frozen after a scene change; a cloud model may genuinely
    # need several seconds, so it keeps the longer limit.
    $hung = if ($state.engine -eq 'local') { 8 } else { 30 }
    if ([bool]$state.busy -and ((Get-Date) - [datetime]$state.busyAt).TotalSeconds -gt $hung) {
        $state.busy = $false
    }
    # A failed request is retried after a cool-down instead of blocking the rest
    # of the sentence: a transient 429, or a key typed a moment later, recovers.
    $wait = if ([int]$state.failedId -eq [int]$state.reqId) { 4000 } else { $gap }
    if (-not [bool]$state.busy -and $txt -ne $state.done -and
            ((Get-Date) - $script:lastTranslateAt).TotalMilliseconds -gt $wait) {
        $script:lastTranslateAt = Get-Date
        $orig = Clip-Text $txt
        # Delta mode: the same sentence grew a few words, so only the tail is
        # sent and the answer is appended to the line already on screen. The
        # request stays short and the finished part is never re-translated, which
        # is what made the Chinese line look like it kept restarting.
        $base = [string]$state.doneDst
        $prev = [string]$state.doneLine
        $tail = ''
        if ([bool]$state.delta -and $base.Length -gt 0 -and $prev.Length -ge 6 -and
                $orig.Length -gt $prev.Length -and
                $orig.StartsWith($prev, [StringComparison]::OrdinalIgnoreCase)) {
            $t = $orig.Substring($prev.Length).Trim()
            if ($t.Length -ge 2 -and $t.Length -le 120) { $tail = $t }
        }
        # The model drops anything after a line break, so lines are fed as one run
        # of text; the overlay still shows the original with its line breaks.
        # Publishing only: the translation worker owns the HTTP call and writes
        # dst, so a slow endpoint can never stall the captions.
        $state.busy = $true
        $state.busyAt = Get-Date
        $state.reqLine = $orig
        $state.reqDelta = ($tail.Length -gt 0)
        $state.reqBase = if ($tail.Length -gt 0) { $base } else { '' }
        $state.req = if ($tail.Length -gt 0) { $tail } else { ($orig -replace "`r?`n", ' ') }
        $state.reqText = $txt
        $state.reqId = [int]$state.reqId + 1
    }

    Start-Sleep -Milliseconds 150
}

Restore-CaptionWindow

# The sidecar owns the GPU process: never leave llama-server running after the
# translation switch is turned off.
Stop-LocalServer
'@

# Translation worker. It only watches the request slot the caption loop fills,
# so the captions keep flowing while a request is in flight.
$trCode = @'
param($state, $rootDir)

$llmPort = 8090

function Log([string]$m) {
    try {
        Add-Content -Path (Join-Path $rootDir 'logs\translate-bridge.log') `
            -Value ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) -Encoding UTF8
    } catch { }
}

Log ('worker start engine=' + [string]$state.engine)
# A single bad caption must never kill the only runspace that translates, so a
# thrown error is logged and the loop carries on with the next request.
trap { Log ('worker error: ' + [string]$_); continue }

function Convert-TargetName([string]$code) {
    switch ($code) {
        'zh-CN' { return 'Simplified Chinese' }
        'zh-TW' { return 'Traditional Chinese' }
        'en'    { return 'English' }
        'ja'    { return 'Japanese' }
        'ko'    { return 'Korean' }
        default { return $code }
    }
}

function Convert-HyName([string]$code) {
    switch ($code) {
        'en-US' { return 'English' }
        'zh-CN' { return 'Chinese' }
        'zh-TW' { return 'Chinese (Traditional)' }
        'ja-JP' { return 'Japanese' }
        'ko-KR' { return 'Korean' }
        'fr-FR' { return 'French' }
        'de-DE' { return 'German' }
        'es-ES' { return 'Spanish' }
        default { return ([string]$code).Split('-')[0] }
    }
}

# Hy-MT2 is driven with its raw training prompt, not a chat template: the LoRA
# was fine-tuned on exactly this sentence pair plus the source text.
function Translate-Local([string]$text) {
    $src = Convert-HyName $state.srcLang
    $dst = Convert-HyName $state.dstLang
    $head = "Translate the following text from $src into $dst. " +
            "Note that you should only output the translated result without any additional explanation:"
    $body = @{
        prompt       = $head + "`n`n" + $text
        n_predict    = 256
        temperature  = 0
        top_k        = 1
        cache_prompt = $true
    } | ConvertTo-Json -Depth 4 -Compress
    $r = Invoke-RestMethod -Uri "http://127.0.0.1:$llmPort/completion" -Method Post `
        -ContentType 'application/json; charset=utf-8' `
        -Body ([Text.Encoding]::UTF8.GetBytes($body)) -TimeoutSec 20
    return ([string]$r.content).Trim()
}

function Translate-Text([string]$text) {
    if ([string]::IsNullOrWhiteSpace($text)) { return '' }
    $sl = ([string]$state.srcLang).Split('-')[0]
    try {
        if ($state.engine -eq 'local') {
            return Translate-Local $text
        }

        if ($state.engine -eq 'openai' -and $state.apiBase) {
            $model = if ($state.model) { $state.model } else { 'gpt-4o-mini' }
            $target = Convert-TargetName $state.dstLang
            $sys = "Translate the user's live subtitle into $target. Reply with the translation only."
            # Uploaded markdown is injected as terminology/context: names and
            # spellings from the document win over the model's own choices.
            if ($state.docText) {
                $sys = $sys + "`n`nReference material. Use its spellings and terminology:`n" + $state.docText
            }
            $body = @{
                model       = $model
                temperature = 0.2
                messages    = @(
                    @{ role = 'system'; content = $sys },
                    @{ role = 'user'; content = $text }
                )
            } | ConvertTo-Json -Depth 6 -Compress
            $headers = @{ 'Content-Type' = 'application/json' }
            if ($state.apiKey) { $headers['Authorization'] = "Bearer $($state.apiKey)" }
            $url = ([string]$state.apiBase).TrimEnd('/') + '/chat/completions'
            $r = Invoke-RestMethod -Uri $url -Method Post -Headers $headers `
                -Body ([Text.Encoding]::UTF8.GetBytes($body)) -TimeoutSec 12
            return ([string]$r.choices[0].message.content).Trim()
        }

        if ($state.engine -ne 'google') {
            $pair = [Uri]::EscapeDataString($sl + '|' + $state.dstLang)
            $url = "https://api.mymemory.translated.net/get?q=" +
                   [Uri]::EscapeDataString($text) + "&langpair=" + $pair
            $r = Invoke-RestMethod -Uri $url -TimeoutSec 8
            return ([string]$r.responseData.translatedText).Trim()
        }

        $url = "https://translate.googleapis.com/translate_a/single?client=gtx&sl=$sl&tl=$($state.dstLang)&dt=t&q=" +
               [Uri]::EscapeDataString($text)
        $r = Invoke-RestMethod -Uri $url -TimeoutSec 8
        $out = ''
        foreach ($seg in $r[0]) { $out += [string]$seg[0] }
        return $out.Trim()
    } catch {
        $state.message = 'translate failed'
        # Kept on the shared state so the panel can report why (401, unknown
        # model, unreachable host) instead of a generic status.
        $msg = [string]$_.Exception.Message
        if ($msg.Length -gt 200) { $msg = $msg.Substring(0, 200) }
        $state.err = $msg
        Log ("translate error: " + $_.Exception.Message)
        return ''
    }
}

$lastId = -1

# Chinese and Japanese do not separate words with spaces, so a delta is glued
# straight on; other targets get one space unless the tail starts with
# punctuation.
function Join-Part([string]$a, [string]$b) {
    if ($a.Length -eq 0) { return $b }
    if ($b.Length -eq 0) { return $a }
    if ($b -match '^[,.!?;:)\]]') { return $a + $b }
    $lang = [string]$state.dstLang
    if ($lang -like 'zh*' -or $lang -like 'ja*' -or $lang -like 'ko*') { return $a + $b }
    return $a + ' ' + $b
}

# Streams the answer and writes it into the shared state while the model is
# still generating, so the first words reach the overlay instead of the whole
# line appearing at once. Returns $null when streaming is not possible and the
# caller repeats the request the blocking way.
function Translate-Stream([string]$text, [int]$id, [string]$base) {
    if ([string]::IsNullOrWhiteSpace($text)) { return $null }
    $url = ''
    $body = ''
    $auth = ''
    try {
        if ($state.engine -eq 'local') {
            $src = Convert-HyName $state.srcLang
            $dst = Convert-HyName $state.dstLang
            $head = "Translate the following text from $src into $dst. " +
                    "Note that you should only output the translated result without any additional explanation:"
            $url = "http://127.0.0.1:$llmPort/completion"
            $body = @{
                prompt       = $head + "`n`n" + $text
                n_predict    = 256
                temperature  = 0
                top_k        = 1
                cache_prompt = $true
                stream       = $true
            } | ConvertTo-Json -Depth 4 -Compress
        } elseif ($state.engine -eq 'openai' -and $state.apiBase) {
            $model = if ($state.model) { $state.model } else { 'gpt-4o-mini' }
            $target = Convert-TargetName $state.dstLang
            $sys = "Translate the user's live subtitle into $target. Reply with the translation only."
            if ($state.docText) {
                $sys = $sys + "`n`nReference material. Use its spellings and terminology:`n" + $state.docText
            }
            $url = ([string]$state.apiBase).TrimEnd('/') + '/chat/completions'
            $body = @{
                model       = $model
                temperature = 0.2
                stream      = $true
                messages    = @(
                    @{ role = 'system'; content = $sys },
                    @{ role = 'user'; content = $text }
                )
            } | ConvertTo-Json -Depth 6 -Compress
            if ($state.apiKey) { $auth = [string]$state.apiKey }
        } else {
            return $null
        }

        $req = [System.Net.HttpWebRequest]::Create($url)
        $req.Method = 'POST'
        $req.ContentType = 'application/json; charset=utf-8'
        $req.Timeout = 15000
        $req.ReadWriteTimeout = 20000
        if ($auth) { $req.Headers.Add('Authorization', "Bearer $auth") }
        $bytes = [Text.Encoding]::UTF8.GetBytes($body)
        $req.ContentLength = $bytes.Length
        $ws = $req.GetRequestStream()
        $ws.Write($bytes, 0, $bytes.Length)
        $ws.Close()
        $resp = $req.GetResponse()
        $reader = New-Object IO.StreamReader($resp.GetResponseStream(), [Text.Encoding]::UTF8)
        $acc = ''
        # MinValue so the very first token is painted immediately: on a fast local
        # model the whole line may arrive inside one 60 ms window, and then the
        # first word would only show up together with the finished sentence.
        $lastPush = [datetime]::MinValue
        while ($true) {
            $line = $reader.ReadLine()
            if ($null -eq $line) { break }
            # A newer caption was published while this one was streaming: stop
            # reading so the worker picks up the new line right away.
            if ([int]$state.reqId -ne $id) { break }
            $line = $line.Trim()
            if ($line.Length -eq 0) { continue }
            if (-not $line.StartsWith('data:')) { continue }
            $payload = $line.Substring(5).Trim()
            if ($payload -eq '[DONE]') { break }
            $j = $null
            try { $j = $payload | ConvertFrom-Json } catch { continue }
            $piece = ''
            if ($null -ne $j.content) { $piece = [string]$j.content }
            elseif ($null -ne $j.choices) { $piece = [string]$j.choices[0].delta.content }
            if ([string]::IsNullOrEmpty($piece)) { continue }
            $acc += $piece
            if (((Get-Date) - $lastPush).TotalMilliseconds -ge 60) {
                $lastPush = Get-Date
                $state.dst = (Join-Part $base $acc)
            }
        }
        $reader.Close()
        $resp.Close()
        return $acc
    } catch {
        Log ("stream error: " + [string]$_.Exception.Message)
        return $null
    }
}

while (-not $state.stop) {
    $id = [int]$state.reqId
    $text = [string]$state.req
    if ($id -eq $lastId -or [string]::IsNullOrWhiteSpace($text)) {
        Start-Sleep -Milliseconds 90
        continue
    }
    # The weights are loaded by the caption loop; until they are ready the same
    # request is retried instead of being marked failed. The slot is released so
    # the caption loop keeps it up to date while the model loads.
    if ($state.engine -eq 'local' -and $state.modelState -ne 'ready') {
        $state.busy = $false
        Start-Sleep -Milliseconds 300
        continue
    }
    $lastId = $id
    $key = [string]$state.reqText
    $base = if ([bool]$state.reqDelta) { [string]$state.reqBase } else { '' }
    # Streaming first: it puts the opening words on screen while the rest of the
    # line is still being generated. Anything it cannot stream falls back to the
    # blocking request, which is what every engine did before.
    $tr = $null
    if ([bool]$state.stream -and ($state.engine -eq 'local' -or ($state.engine -eq 'openai' -and $state.apiBase))) {
        $tr = Translate-Stream $text $id $base
    }
    if ($null -eq $tr) { $tr = Translate-Text $text }
    Log ('req ' + $id + ' in=' + $text.Length + ' base=' + $base.Length + ' out=' + ([string]$tr).Length)
    # The line was cleared while this request was in flight: its answer belongs
    # to text that is no longer on screen.
    $stale = ([int]$state.reqId -ne $id)
    $state.busy = $false
    if ($stale) { continue }
    if ($tr.Length -gt 0) {
        $full = Join-Part $base ([string]$tr)
        $state.dst = $full
        $state.done = $key
        $state.doneId = $id
        # Delta mode compares the next caption against these two, so they must
        # describe the answer that is actually on screen.
        $state.doneLine = [string]$state.reqLine
        $state.doneDst = $full
        $state.message = 'running'
        $state.err = ''
    } else {
        $state.failedId = $id
    }
    Start-Sleep -Milliseconds 60
}
'@

# Only one sidecar per port: a second copy would stack a second subtitle window
# (two overlays with different revisions is exactly what garbled subtitles look
# like), and it could never be controlled by the panel.
$alreadyAlive = $false
try {
    $probe = Invoke-WebRequest "http://127.0.0.1:$Port/state" -UseBasicParsing -TimeoutSec 2
    $alreadyAlive = ($probe.StatusCode -eq 200)
} catch { }
if ($alreadyAlive) {
    Write-Log 'another sidecar already owns the port, exit'
    exit
}

$httpJob = [PowerShell]::Create()
[void]$httpJob.AddScript($httpCode).AddArgument($state).AddArgument($Port).AddArgument($cfgFile)
[void]$httpJob.BeginInvoke()

$asrJob = [PowerShell]::Create()
[void]$asrJob.AddScript($asrCode).AddArgument($state).AddArgument($Port).AddArgument($rootDir)
[void]$asrJob.BeginInvoke()

$trJob = [PowerShell]::Create()
[void]$trJob.AddScript($trCode).AddArgument($state).AddArgument($rootDir)
[void]$trJob.BeginInvoke()

# --- overlay ---------------------------------------------------------------
$win = New-Object System.Windows.Window
$win.WindowStyle = 'None'
$win.AllowsTransparency = $true
$win.Background = [System.Windows.Media.Brushes]::Transparent
# The subtitle is part of the picture, not a floating panel: it is owned by the
# Veyra window (see the Owner assignment in Update-Overlay), so Windows keeps it
# directly above the video, hides it together with Veyra and never lets it sit on
# top of another application. It must therefore not be topmost.
$win.Topmost = $false
$win.ShowInTaskbar = $false
$win.ShowActivated = $false
$win.ResizeMode = 'NoResize'
$win.SizeToContent = 'WidthAndHeight'

$border = New-Object System.Windows.Controls.Border
$border.CornerRadius = New-Object System.Windows.CornerRadius -ArgumentList 10
$border.Padding = New-Object System.Windows.Thickness -ArgumentList 18, 7, 18, 7
$border.HorizontalAlignment = 'Center'
$plateBrush = New-Object System.Windows.Media.SolidColorBrush -ArgumentList ([System.Windows.Media.Color]::FromArgb(150, 0, 0, 0))
$border.Background = $plateBrush
$script:plateOn = $true

$panel = New-Object System.Windows.Controls.StackPanel
$panel.HorizontalAlignment = 'Center'

# Subtitle glyphs are drawn as geometry, not as a TextBlock: a TextBlock only has
# a blurred drop shadow, and the request was for a real outline plus a shadow.
# The outline is the glyph geometry stroked with a black pen, the shadow is the
# same geometry stamped a few times below the text.
# Add-Type needs the WPF assemblies by path: bare names ("PresentationCore.dll")
# are not resolved by the 5.1 compiler.
$vyRefs = @(
    [System.Windows.DependencyObject].Assembly.Location,
    [System.Windows.Media.Brush].Assembly.Location,
    [System.Windows.Window].Assembly.Location,
    [System.Xaml.XamlReader].Assembly.Location
)
Add-Type -ReferencedAssemblies $vyRefs -TypeDefinition @"
using System;
using System.Globalization;
using System.Windows;
using System.Windows.Media;

public class VyOutlinedText : FrameworkElement
{
    public static readonly DependencyProperty TextProperty = DependencyProperty.Register(
        "Text", typeof(string), typeof(VyOutlinedText),
        new FrameworkPropertyMetadata("", FrameworkPropertyMetadataOptions.AffectsMeasure | FrameworkPropertyMetadataOptions.AffectsRender));
    public static readonly DependencyProperty FontSizeProperty = DependencyProperty.Register(
        "FontSize", typeof(double), typeof(VyOutlinedText),
        new FrameworkPropertyMetadata(26.0, FrameworkPropertyMetadataOptions.AffectsMeasure | FrameworkPropertyMetadataOptions.AffectsRender));
    public static readonly DependencyProperty FontFamilyProperty = DependencyProperty.Register(
        "FontFamily", typeof(string), typeof(VyOutlinedText),
        new FrameworkPropertyMetadata("Segoe UI", FrameworkPropertyMetadataOptions.AffectsMeasure | FrameworkPropertyMetadataOptions.AffectsRender));
    public static readonly DependencyProperty ForegroundProperty = DependencyProperty.Register(
        "Foreground", typeof(Brush), typeof(VyOutlinedText),
        new FrameworkPropertyMetadata(Brushes.White, FrameworkPropertyMetadataOptions.AffectsRender));
    public static readonly DependencyProperty StrokeBrushProperty = DependencyProperty.Register(
        "StrokeBrush", typeof(Brush), typeof(VyOutlinedText),
        new FrameworkPropertyMetadata(Brushes.Black, FrameworkPropertyMetadataOptions.AffectsRender));
    public static readonly DependencyProperty StrokeThicknessProperty = DependencyProperty.Register(
        "StrokeThickness", typeof(double), typeof(VyOutlinedText),
        new FrameworkPropertyMetadata(2.0, FrameworkPropertyMetadataOptions.AffectsMeasure | FrameworkPropertyMetadataOptions.AffectsRender));
    public static readonly DependencyProperty ShadowDepthProperty = DependencyProperty.Register(
        "ShadowDepth", typeof(double), typeof(VyOutlinedText),
        new FrameworkPropertyMetadata(2.0, FrameworkPropertyMetadataOptions.AffectsRender));
    public static readonly DependencyProperty ShadowOpacityProperty = DependencyProperty.Register(
        "ShadowOpacity", typeof(double), typeof(VyOutlinedText),
        new FrameworkPropertyMetadata(0.8, FrameworkPropertyMetadataOptions.AffectsRender));

    public string Text { get { return (string)GetValue(TextProperty); } set { SetValue(TextProperty, value); } }
    public double FontSize { get { return (double)GetValue(FontSizeProperty); } set { SetValue(FontSizeProperty, value); } }
    public string FontFamily { get { return (string)GetValue(FontFamilyProperty); } set { SetValue(FontFamilyProperty, value); } }
    public Brush Foreground { get { return (Brush)GetValue(ForegroundProperty); } set { SetValue(ForegroundProperty, value); } }
    public Brush StrokeBrush { get { return (Brush)GetValue(StrokeBrushProperty); } set { SetValue(StrokeBrushProperty, value); } }
    public double StrokeThickness { get { return (double)GetValue(StrokeThicknessProperty); } set { SetValue(StrokeThicknessProperty, value); } }
    public double ShadowDepth { get { return (double)GetValue(ShadowDepthProperty); } set { SetValue(ShadowDepthProperty, value); } }
    public double ShadowOpacity { get { return (double)GetValue(ShadowOpacityProperty); } set { SetValue(ShadowOpacityProperty, value); } }

    private FormattedText Build(double limit)
    {
        string family = string.IsNullOrEmpty(FontFamily) ? "Segoe UI" : FontFamily;
        Typeface face = new Typeface(new FontFamily(family), FontStyles.Normal, FontWeights.Normal, FontStretches.Normal);
        FormattedText ft = new FormattedText(
            Text == null ? string.Empty : Text,
            CultureInfo.CurrentUICulture,
            FlowDirection.LeftToRight,
            face,
            Math.Max(1.0, FontSize),
            Foreground == null ? Brushes.White : Foreground,
            VisualTreeHelper.GetDpi(this).PixelsPerDip);
        ft.MaxTextWidth = limit > 0 ? limit : 4096.0;
        ft.TextAlignment = TextAlignment.Center;
        return ft;
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        if (string.IsNullOrEmpty(Text)) return new Size(0, 0);
        double limit = MaxWidth;
        if (double.IsNaN(limit) || double.IsInfinity(limit) || limit <= 0) limit = 4096.0;
        FormattedText ft = Build(limit);
        // One spare pixel plus the pen width: the render pass lays the text out
        // again inside the arranged width, and rounding must not wrap it anew.
        return new Size(Math.Ceiling(ft.Width) + StrokeThickness + 2, Math.Ceiling(ft.Height) + 2);
    }

    protected override void OnRender(DrawingContext dc)
    {
        string text = Text;
        if (string.IsNullOrEmpty(text)) return;
        double width = ActualWidth;
        if (width <= 0) return;
        FormattedText ft = Build(width);
        Geometry glyphs = ft.BuildGeometry(new Point(0, 0));
        if (glyphs == null || glyphs.IsEmpty()) return;

        double depth = ShadowDepth;
        double penWidth = StrokeThickness * 2;
        if (depth > 0 && ShadowOpacity > 0)
        {
            byte alpha = (byte)Math.Max(0, Math.Min(255, ShadowOpacity * 255 / 3));
            SolidColorBrush shadow = new SolidColorBrush(Color.FromArgb(alpha, 0, 0, 0));
            shadow.Freeze();
            Pen shadowPen = new Pen(shadow, penWidth);
            shadowPen.LineJoin = PenLineJoin.Round;
            shadowPen.Freeze();
            for (int i = 1; i <= 3; i++)
            {
                dc.PushTransform(new TranslateTransform(0, depth * i * 0.66));
                dc.DrawGeometry(shadow, shadowPen, glyphs);
                dc.Pop();
            }
        }

        // Outline first, fill afterwards: a pen strokes the outline of the glyph
        // shape, so painting the stroke last eats half the letter at subtitle
        // sizes (the text then reads as dark blobs). With the fill painted on
        // top only the outer half of the pen remains visible, which is exactly
        // the halo around the glyphs and keeps the letters solid.
        if (penWidth > 0 && StrokeBrush != null)
        {
            Pen outline = new Pen(StrokeBrush, penWidth);
            outline.LineJoin = PenLineJoin.Round;
            outline.Freeze();
            dc.DrawGeometry(null, outline, glyphs);
        }
        dc.DrawGeometry(Foreground == null ? Brushes.White : Foreground, null, glyphs);
    }
}
"@

function New-VyText([double]$size, [string]$family, [string]$color) {
    $tb = New-Object VyOutlinedText
    $tb.FontFamily = $family
    $tb.FontSize = $size
    $tb.Foreground = (New-Object System.Windows.Media.BrushConverter).ConvertFromString($color)
    $tb.StrokeBrush = (New-Object System.Windows.Media.BrushConverter).ConvertFromString('#000000')
    $tb.StrokeThickness = [Math]::Max(1.0, $size * 0.055)
    $tb.ShadowDepth = [Math]::Max(2.0, $size * 0.09)
    $tb.HorizontalAlignment = 'Center'
    return $tb
}

$zhText = New-VyText 26 'Microsoft YaHei UI' '#FFFFFF'
$enText = New-VyText 18 'Segoe UI' '#DCDCDC'
# Each line gets its own vertical offset so a new caption can rise into place.
$zhText.RenderTransform = New-Object System.Windows.Media.TranslateTransform
$enText.RenderTransform = New-Object System.Windows.Media.TranslateTransform

[void]$panel.Children.Add($zhText)
[void]$panel.Children.Add($enText)
$border.Child = $panel
$win.Content = $border

$win.Add_SourceInitialized({
    $helper = New-Object System.Windows.Interop.WindowInteropHelper -ArgumentList $win
    $script:overlayHwnd = $helper.Handle
    $h = $script:overlayHwnd
    $ex = [VyNative]::GetWindowLong($h, -20)
    # WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
    [void][VyNative]::SetWindowLong($h, -20, ($ex -bor 0x20 -bor 0x80000 -bor 0x08000000 -bor 0x80))
})

# Create the (still hidden) handle now: the plate has to be owned by Veyra
# before its first visible frame, otherwise that frame is drawn without an owner
# and lands above every other window for a moment.
try {
    $bootHelper = New-Object System.Windows.Interop.WindowInteropHelper -ArgumentList $win
    [void]$bootHelper.EnsureHandle()
} catch {
    Write-Log ('handle create failed: ' + $_.Exception.Message)
}

$script:missingTicks = 0
$script:overlayErrors = 0
# Handle of the Veyra window the plate is owned by; rebound if the app restarts.
$script:ownerHwnd = [IntPtr]::Zero
# Fade state of the subtitle plate: 'idle' | 'in' | 'out'. A WPF opacity
# animation replaces the instant Hide(), so a cleared line fades away instead of
# blinking out and the next one fades back in.
$script:fadeState = 'idle'
$script:fadeAt = Get-Date
$script:noTextSince = Get-Date
$script:picking = $false

function Start-Fade {
    $anim = New-Object System.Windows.Media.Animation.DoubleAnimation
    $anim.From = 0.0
    $anim.To = 1.0
    $anim.Duration = [System.Windows.Duration]::new([TimeSpan]::FromMilliseconds(170))
    $border.BeginAnimation([System.Windows.UIElement]::OpacityProperty, $anim)
}

# A brand new line (translation arrives, or the next sentence replaces the old
# one) fades in and rises a few pixels. Word by word growth is left instant:
# caption text is rewritten several times a second, and animating every rewrite
# made the whole line strobe.
function Start-Text-Fade($t) {
    $ease = New-Object System.Windows.Media.Animation.CubicEase
    $ease.EasingMode = 'EaseOut'
    $a = New-Object System.Windows.Media.Animation.DoubleAnimation
    $a.From = 0.0
    $a.To = 1.0
    $a.Duration = [System.Windows.Duration]::new([TimeSpan]::FromMilliseconds(220))
    $a.EasingFunction = $ease
    $t.BeginAnimation([System.Windows.UIElement]::OpacityProperty, $a)
    $m = $t.RenderTransform
    if ($m -is [System.Windows.Media.TranslateTransform]) {
        $rise = New-Object System.Windows.Media.Animation.DoubleAnimation
        $rise.From = 7.0
        $rise.To = 0.0
        $rise.Duration = [System.Windows.Duration]::new([TimeSpan]::FromMilliseconds(220))
        $rise.EasingFunction = $ease
        $m.BeginAnimation([System.Windows.Media.TranslateTransform]::YProperty, $rise)
    }
}

function Show-Overlay {
    if (-not $win.IsVisible) {
        # The animation starts before the window is shown, so its first visible
        # frame is already at opacity 0 instead of flashing in at full strength.
        Start-Fade
        $win.Show()
        $win.UpdateLayout()
        Write-Log 'overlay shown'
    } elseif ($script:fadeState -eq 'out') {
        # The line came back while it was still fading out: run the fade
        # backwards from the current opacity instead of restarting it.
        $anim = New-Object System.Windows.Media.Animation.DoubleAnimation
        $anim.To = 1.0
        $anim.Duration = [System.Windows.Duration]::new([TimeSpan]::FromMilliseconds(120))
        $border.BeginAnimation([System.Windows.UIElement]::OpacityProperty, $anim)
    } elseif ($script:fadeState -eq 'idle') {
        # The plate was already cleared to opacity 0 but the window stayed up, so
        # only the opacity has to come back. A visible plate ('in') is left alone:
        # restarting the fade on every caption tick made the line strobe.
        Start-Fade
    }
    $script:fadeState = 'in'
    $script:fadeAt = Get-Date
}

function Hide-Overlay {
    if (-not $win.IsVisible) { $script:fadeState = 'idle'; return }
    if ($script:fadeState -eq 'out') { return }
    $anim = New-Object System.Windows.Media.Animation.DoubleAnimation
    $anim.To = 0.0
    $anim.Duration = [System.Windows.Duration]::new([TimeSpan]::FromMilliseconds(180))
    $border.BeginAnimation([System.Windows.UIElement]::OpacityProperty, $anim)
    $script:fadeState = 'out'
    $script:fadeAt = Get-Date
}

# Called on the next tick: the plate is only cleared once the fade finished.
# The window itself stays up, fully transparent: Hide() + Show() re-creates a
# layered per-pixel-alpha window every time a line comes back after a pause,
# and that recreate is what made a new sentence take seconds to appear.
function Finish-Fade {
    if ($script:fadeState -ne 'out') { return }
    if (((Get-Date) - $script:fadeAt).TotalMilliseconds -lt 200) { return }
    $border.BeginAnimation([System.Windows.UIElement]::OpacityProperty, $null)
    $border.Opacity = 0.0
    $script:fadeState = 'idle'
}

function Update-Overlay {
  try {
    $script:overlayErrors = 0
    if ($state.stop) {
        $timer.Stop()
        $win.Close()
        # Dispatcher.Run keeps spinning after the last window closes, so the
        # shutdown has to be requested or this process would stay behind.
        $win.Dispatcher.InvokeShutdown()
        return
    }

    Finish-Fade
    if ($script:picking) { return }

    # Reference document: either loaded from a path the panel sent, or picked in
    # the WPF dialog below (a file dialog can only open on this thread).
    if ($state.docLoad) {
        $p = [string]$state.docLoad
        $state.docLoad = ''
        Read-DocFile $p
        Save-Cfg
    }
    if ($state.docClear) {
        $state.docClear = $false
        Read-DocFile ''
        Save-Cfg
    }
    if ($state.pickRequest) {
        $state.pickRequest = $false
        $script:picking = $true
        try {
            $dlg = New-Object Microsoft.Win32.OpenFileDialog
            $dlg.Title = 'Veyra: reference document for translation'
            $dlg.Filter = 'Markdown (*.md;*.markdown;*.txt)|*.md;*.markdown;*.txt|All files (*.*)|*.*'
            $dlg.CheckFileExists = $true
            $picked = $dlg.ShowDialog()
            if ($picked -eq $true -and $dlg.FileName) {
                Read-DocFile $dlg.FileName
                Save-Cfg
            }
        } catch {
            Write-Log ('doc pick failed: ' + $_.Exception.Message)
        }
        $script:picking = $false
        return
    }

    $zhText.FontSize = [Math]::Max(12, [double]$state.fontSize)
    $enText.FontSize = [Math]::Max(11, [double]$state.fontSize - 8)

    # Outline and shadow are panel settings. Both are quoted for a 26 px caption
    # and scaled with it, so a 44 px caption keeps the same visual weight.
    foreach ($t in @($zhText, $enText)) {
        $k = $t.FontSize / 26.0
        $stroke = if ($state.stroke) { [Math]::Max(0.0, [double]$state.strokeW) * $k } else { 0.0 }
        $depth = if ($state.shadow) { [Math]::Max(0.0, [double]$state.shadowD) * $k } else { 0.0 }
        # Guarded: an unchanged double still invalidates the render pass.
        if ($t.StrokeThickness -ne $stroke) { $t.StrokeThickness = $stroke }
        if ($t.ShadowDepth -ne $depth) { $t.ShadowDepth = $depth }
    }

    switch ([string]$state.mode) {
        'translated' { $zhText.Visibility = 'Visible'; $enText.Visibility = 'Collapsed' }
        'original'   { $zhText.Visibility = 'Collapsed'; $enText.Visibility = 'Visible'; $enText.FontSize = [Math]::Max(12, [double]$state.fontSize) }
        default      { $zhText.Visibility = 'Visible'; $enText.Visibility = 'Visible' }
    }

    $zh = [string]$state.dst
    $en = [string]$state.raw
    $hasText = ($zh.Length -gt 0) -or ($en.Length -gt 0)
    if (-not $hasText) {
        # Live Captions blanks its line between sentences for a few hundred ms;
        # fading on every such gap made the subtitle strobe, so the fade starts
        # only after the line stayed empty for a moment. The last text is kept in
        # the elements, which is what the fade draws.
        if (((Get-Date) - $script:noTextSince).TotalMilliseconds -gt 350) { Hide-Overlay }
        return
    }
    $script:noTextSince = Get-Date
    if ($zhText.Text -ne $zh) {
        $prevZh = [string]$zhText.Text
        $zhText.Text = $zh
        if ($state.anim -and ($prevZh.Length -eq 0 -or -not $zh.StartsWith($prevZh))) {
            Start-Text-Fade $zhText
        }
    }
    if ($enText.Text -ne $en) {
        $prevEn = [string]$enText.Text
        $enText.Text = $en
        if ($state.anim -and ($prevEn.Length -eq 0 -or -not $en.StartsWith($prevEn))) {
            Start-Text-Fade $enText
        }
    }

    # The panel sends an empty rectangle for pages that have no picture (home,
    # settings, export). The subtitle belongs to the video only, so it is
    # hidden there instead of being left over the previous page's geometry.
    if ($state.videoGiven -and ($state.videoW -le 0 -or $state.videoH -le 0)) {
        Hide-Overlay
        return
    }

    # Re-brushed only when the switch flips: a fresh brush on every tick
    # invalidated the layered window five times a second, which showed up as a
    # faint blink of the whole plate on top of the text changes.
    $wantPlate = [bool]$state.backdrop
    if ($wantPlate -ne $script:plateOn) {
        $script:plateOn = $wantPlate
        $border.Background = if ($wantPlate) { $plateBrush } else { [System.Windows.Media.Brushes]::Transparent }
    }

    $p = Get-Process veyra_qml_ui -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $p) {
        $script:missingTicks++
        if ($script:missingTicks -gt 25) { $state.stop = $true }
        return
    }
    $script:missingTicks = 0
    $p.Refresh()
    $h = $p.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { return }

    # Own the plate with the Veyra window once per handle: Windows then keeps it
    # directly above the video for the whole session, hides it together with
    # Veyra and never draws it over another application. This replaced a
    # foreground-process check that hid the plate whenever the user alt-tabbed
    # away and showed it again on the way back - and every Show() of a layered,
    # per-pixel-alpha WPF window is a full re-creation, which is the multi-second
    # stall that followed switching back to the picture.
    if ($script:ownerHwnd -ne $h -and $script:overlayHwnd -ne [IntPtr]::Zero) {
        try {
            $ownHelper = New-Object System.Windows.Interop.WindowInteropHelper -ArgumentList $win
            $ownHelper.Owner = $h
            $script:ownerHwnd = $h
            Write-Log 'overlay owned by veyra'
        } catch {
            Write-Log ('owner bind failed: ' + $_.Exception.Message)
        }
    }
    # Nothing to caption over while Veyra is minimised: the owned window is
    # hidden along with its owner, so only the layout work is skipped here.
    if ([VyNative]::IsIconic($h)) { return }

    $r = New-Object VyNative+RECT
    if (-not [VyNative]::GetWindowRect($h, [ref]$r)) { return }

    # The subtitle lives inside the picture, not inside the window: the panel
    # reports the viewport (in client coordinates) of the page that owns the
    # video, so the plate is centred on the picture and clamped to it even when
    # the inspector or the transport bar takes part of the window.
    $areaLeft = $r.Left
    $areaTop = $r.Top
    $areaW = $r.Right - $r.Left
    $areaH = $r.Bottom - $r.Top
    $scale = [VyNative]::GetDpiForWindow($h) / 96.0
    if ($scale -le 0) { $scale = 1.0 }
    if ($state.videoW -gt 0 -and $state.videoH -gt 0) {
        $origin = New-Object VyNative+POINT
        $origin.X = 0
        $origin.Y = 0
        if ([VyNative]::ClientToScreen($h, [ref]$origin)) {
            $areaLeft = $origin.X + [int]($state.videoX * $scale)
            $areaTop = $origin.Y + [int]($state.videoY * $scale)
            $areaW = [int]($state.videoW * $scale)
            $areaH = [int]($state.videoH * $scale)
        }
    }

    # Lay the text out at 72 % of the video width. MaxWidth (not Width) is used
    # on purpose: a pinned Width makes DesiredSize report that width even when
    # the text needs less, and a long line that arrives later then overflows the
    # plate and gets cut at its right edge. With MaxWidth the block wraps at the
    # limit and reports the width its longest line really needs, so
    # SizeToContent can shrink or grow the plate to match.
    # The layout width is capped by the picture too: in a small viewport the 72 %
    # rule alone would produce a plate wider than the video, and a plate wider
    # than its area can only be clamped to the area's left edge while still
    # hanging over whatever is beside it. The plate's own padding (18 + 18) and
    # the outline margin are outside the text width, so they are discounted.
    $maxW = [Math]::Max(320.0, ($areaW * 0.72) / $scale)
    $fitW = ($areaW / $scale) - 48
    if ($maxW -gt $fitW) { $maxW = [Math]::Max(80.0, $fitW) }
    $measure = [System.Windows.Size]::new($maxW, [double]::PositiveInfinity)
    foreach ($tb in @($zhText, $enText)) {
        if ([double]::IsNaN($tb.MaxWidth) -or [Math]::Abs($tb.MaxWidth - $maxW) -gt 1) { $tb.MaxWidth = $maxW }
        $tb.Width = [double]::NaN
        if ($tb.Visibility -ne 'Visible') { continue }
        $tb.Measure($measure)
    }

    # SizeToContent only re-measures on request: without this a line that grew
    # past the last layout stays clipped instead of wrapping.
    $win.UpdateLayout()

    Show-Overlay

    $self = New-Object VyNative+RECT
    if (-not [VyNative]::GetWindowRect($script:overlayHwnd, [ref]$self)) { return }
    $wpx = $self.Right - $self.Left
    $hpx = $self.Bottom - $self.Top
    if ($wpx -le 0 -or $hpx -le 0) { return }

    # Screen pixels on both sides: the process DPI context is shared, so the
    # coordinates stay consistent between Veyra's rect and the overlay.
    $margin = [int]($state.inset * $scale)
    $left = $areaLeft + [int](($areaW - $wpx) / 2)
    switch ([string]$state.posH) {
        'left'  { $left = $areaLeft + $margin }
        'right' { $left = $areaLeft + $areaW - $wpx - $margin }
    }
    switch ([string]$state.posV) {
        'top'    { $top = $areaTop + $margin }
        'center' { $top = $areaTop + [int](($areaH - $hpx) / 2) }
        default  { $top = $areaTop + $areaH - $hpx - $margin }
    }

    # Clamped to the picture: a long plate in a small viewport is pushed back
    # inside instead of hanging over the inspector, the transport bar or the
    # desktop.
    $maxLeft = $areaLeft + $areaW - $wpx
    if ($left -gt $maxLeft) { $left = $maxLeft }
    if ($left -lt $areaLeft) { $left = $areaLeft }
    $maxTop = $areaTop + $areaH - $hpx
    if ($top -gt $maxTop) { $top = $maxTop }
    if ($top -lt $areaTop) { $top = $areaTop }

    # SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER: the owner chain decides the
    # z-order now, so the position call must not fight it.
    [void][VyNative]::SetWindowPos($script:overlayHwnd, [IntPtr]::Zero, $left, $top, 0, 0, 0x0001 -bor 0x0010 -bor 0x0004)
  } catch {
    # One failed tick must not freeze the subtitle for the rest of the session;
    # only give up when it keeps failing.
    $script:overlayErrors++
    Write-Log ("overlay error (" + $script:overlayErrors + "): " + $_.Exception.Message)
    if ($script:overlayErrors -gt 25) { $timer.Stop() }
  }
}

# A WINEVENT_OUTOFCONTEXT hook delivers its callbacks through the message loop
# of the thread that installed it, so it is installed here: the WPF dispatcher
# below is that loop. Live Captions can be restarted, and a restart changes the
# process id, so the hook is re-armed whenever the caption loop reports a new one.
$hookTimer = New-Object System.Windows.Threading.DispatcherTimer
$hookTimer.Interval = [TimeSpan]::FromMilliseconds(1000)
$hookTimer.Add_Tick({
    try {
        $capId = [int]$state.capPid
        if ($capId -ne 0 -and [VyHook]::targetPid -ne $capId) {
            [VyHook]::Install([uint32]$capId)
            [VyHook]::targetPid = $capId
            Write-Log ('caption hook armed pid=' + $capId)
        }
    } catch {
        Write-Log ('hook error: ' + $_.Exception.Message)
    }
})
$hookTimer.Start()

$timer = New-Object System.Windows.Threading.DispatcherTimer
$timer.Interval = [TimeSpan]::FromMilliseconds(200)
$timer.Add_Tick({ Update-Overlay })
$timer.Start()

Write-Log 'overlay ready'
try {
    [System.Windows.Threading.Dispatcher]::Run()
} catch {
    Write-Log ("dispatcher error: " + $_.Exception.Message)
}
Write-Log 'dispatcher returned'

$state.stop = $true
Start-Sleep -Milliseconds 300
try { $httpJob.Stop() } catch { }
try { $asrJob.Stop() } catch { }
try { $trJob.Stop() } catch { }
# The ASR job may be killed mid-wait, so the GPU process is reaped here as well.
if ($state.llmPid) {
    try { Get-Process -Id $state.llmPid -ErrorAction SilentlyContinue | Stop-Process -Force } catch { }
}
# Same for the caption window: close the one this sidecar opened, put back the
# one the user already had open.
if ($state.lcSpawned) {
    try { Get-Process LiveCaptions -ErrorAction SilentlyContinue | Stop-Process -Force } catch { }
} elseif ($state.lcHwnd) {
    try { [void][VyNative]::SetWindowPos([IntPtr]$state.lcHwnd, [IntPtr]::Zero, [int]$state.lcLeft, [int]$state.lcTop, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) } catch { }
}
Write-Log 'stopped'
