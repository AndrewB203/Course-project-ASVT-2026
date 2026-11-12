#requires -version 5.1
# OVS site smoke test v4. ASCII-only, robust JS scanning.
[CmdletBinding()]
param(
    [string]$Root,
    [int]$Port = 8080,
    [string]$DeviceUrl = 'http://192.168.137.35',
    [string]$PublicUrl,
    [string]$BackendUrl,
    [switch]$Lan,
    [switch]$NoServe,
    [switch]$NoBrowser,
    [switch]$Once
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
Add-Type -AssemblyName System.Net.Http

$Results = New-Object 'System.Collections.Generic.List[object]'
$script:Http = $null

function Add-Result {
    param([string]$Scope, [string]$Name, [ValidateSet('OK','WARN','FAIL','INFO')][string]$Status, [string]$Detail)
    $Results.Add([pscustomobject]@{ Scope=$Scope; Name=$Name; Status=$Status; Detail=$Detail })
    $color = switch ($Status) { 'OK' {'Green'} 'WARN' {'Yellow'} 'FAIL' {'Red'} default {'Cyan'} }
    Write-Host ("[{0,-4}] {1} - {2}" -f $Status, $Name, $Detail) -ForegroundColor $color
}

function Get-HttpClient {
    if ($script:Http) { return $script:Http }
    $h = [System.Net.Http.HttpClientHandler]::new()
    $h.UseProxy = $false
    $h.AllowAutoRedirect = $true
    $c = [System.Net.Http.HttpClient]::new($h)
    $c.Timeout = [TimeSpan]::FromSeconds(8)
    $script:Http = $c
    return $c
}

function Invoke-Http {
    param([string]$Url)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $resp = (Get-HttpClient).GetAsync($Url).GetAwaiter().GetResult()
        $body = $resp.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        $ct = ''
        if ($resp.Content.Headers.ContentType) { $ct = $resp.Content.Headers.ContentType.ToString() }
        $server = ''; $vals = $null
        if ($resp.Headers.TryGetValues('Server', [ref]$vals)) { $server = ($vals -join ',') }
        $code = [int]$resp.StatusCode
        $resp.Dispose()
        $sw.Stop()
        return [pscustomobject]@{ Ok=$true; Status=$code; Content=$body; ContentType=$ct; Server=$server; Ms=$sw.ElapsedMilliseconds; Error=$null }
    } catch {
        $sw.Stop()
        $msg = $_.Exception.Message
        if ($_.Exception.InnerException) { $msg = $_.Exception.InnerException.Message }
        return [pscustomobject]@{ Ok=$false; Status=0; Content=''; ContentType=''; Server=''; Ms=$sw.ElapsedMilliseconds; Error=$msg }
    }
}

function Get-HtmlTitle {
    param([string]$Html)
    if ($Html -match '(?is)<title[^>]*>(.*?)</title>') { return $Matches[1].Trim() }
    return ''
}

function Resolve-ProjectRoot {
    $dirs = @()
    if ($PSScriptRoot) { $dirs += $PSScriptRoot; $dirs += (Split-Path $PSScriptRoot -Parent) }
    $dirs += (Get-Location).Path
    foreach ($d in $dirs) {
        if (-not $d) { continue }
        foreach ($marker in 'frontend','docs','index.html','.git','Makefile') {
            if (Test-Path (Join-Path $d $marker)) { return (Resolve-Path $d).Path }
        }
    }
    return (Get-Location).Path
}

function Resolve-SiteRoot {
    param([string]$ProjectRoot)
    if ($Root) {
        $p = (Resolve-Path $Root).Path
        if (Test-Path (Join-Path $p 'index.html')) { return $p }
        throw "No index.html in '$p'"
    }
    foreach ($c in 'frontend','frontend\dist','frontend\build','docs','.') {
        $p = Join-Path $ProjectRoot $c
        if (Test-Path (Join-Path $p 'index.html')) { return (Resolve-Path $p).Path }
    }
    return $null
}

function Test-StaticSite {
    param([string]$SiteRoot)
    $index = Join-Path $SiteRoot 'index.html'
    $fi = Get-Item $index
    $html = Get-Content $index -Raw -Encoding UTF8
    Add-Result 'Files' 'index.html' 'OK' ("{0} - {1:N0} bytes" -f $fi.FullName, $fi.Length)
    if ($fi.Length -lt 200) { Add-Result 'Files' 'Page size' 'WARN' 'Suspiciously small file' }
    $checks = @(
        @('DOCTYPE',       '(?i)<!doctype\s+html'),
        @('html lang',     '(?i)<html[^>]*\slang\s*='),
        @('meta charset',  '(?i)<meta[^>]+charset'),
        @('meta viewport', '(?i)<meta[^>]+name\s*=\s*["'']viewport["'']'),
        @('title',         '(?is)<title[^>]*>\s*\S')
    )
    foreach ($c in $checks) {
        if ($html -match $c[1]) { Add-Result 'Files' $c[0] 'OK' 'Present' }
        else { Add-Result 'Files' $c[0] 'FAIL' 'Missing in index.html' }
    }
    if ($html -match '(?i)\b(TODO|FIXME|lorem ipsum)\b') {
        Add-Result 'Files' 'Unfinished markers' 'WARN' 'TODO/FIXME found - verify not visible text'
    } else { Add-Result 'Files' 'Unfinished markers' 'OK' 'None found' }
    return $html
}

function Get-LocalAssets {
    param([string]$Html, [string]$SiteRoot)
    $rx = [regex]"(?i)(?:src|href)\s*=\s*[""']([^""']+)[""']"
    $list = New-Object 'System.Collections.Generic.List[object]'
    $seen = @{}; $dyn = 0; $api = 0
    foreach ($m in $rx.Matches($Html)) {
        $ref = $m.Groups[1].Value.Trim()
        if ($ref -match '^(#|data:|mailto:|tel:|javascript:|https?:|//)') { continue }
        if ($ref -match '\$\{' -or $ref -match '\{\{') { $dyn++; continue }
        if ($ref -match '^/api(/|$|\?)') { $api++; continue }
        $clean = ($ref -split '[?#]')[0]
        if (-not $clean) { continue }
        $rel = $clean.TrimStart('/') -replace '/', '\'
        $full = [IO.Path]::GetFullPath((Join-Path $SiteRoot $rel))
        if ($seen.ContainsKey($full)) { continue }
        $seen[$full] = $true
        $list.Add([pscustomobject]@{ Ref=$ref; Full=$full; Exists=(Test-Path $full -PathType Leaf) })
    }
    return [pscustomobject]@{ List=$list; Dynamic=$dyn; Api=$api }
}

function Get-FreePort {
    param([int]$Start)
    for ($p = $Start; $p -lt ($Start + 25); $p++) {
        $l = $null
        try { $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $p); $l.Start(); return $p }
        catch {} finally { if ($l) { $l.Stop() } }
    }
    throw 'No free port found'
}

function Get-PythonCommand {
    foreach ($name in 'py','python','python3') {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if (-not $cmd) { continue }
        try {
            if ($name -eq 'py') { & $name -3 -c "import sys" 2>$null; if ($LASTEXITCODE -eq 0) { return ,@('py','-3') } }
            else { & $name -c "import sys" 2>$null; if ($LASTEXITCODE -eq 0) { return ,@($name) } }
        } catch {}
    }
    return $null
}

function Wait-Http {
    param([string]$Url, [int]$TimeoutSec = 15)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec) {
        $r = Invoke-Http -Url $Url
        if ($r.Ok -and $r.Status -eq 200) { return $true }
        Start-Sleep -Milliseconds 400
    }
    return $false
}

function Start-HttpListenerJob {
    param([string]$SiteRoot, [int]$Port)
    return Start-Job -Name 'ovs-site-server' -ArgumentList $SiteRoot,$Port -ScriptBlock {
        param($root,$port)
        $mime = @{ '.html'='text/html; charset=utf-8'; '.htm'='text/html; charset=utf-8'; '.css'='text/css; charset=utf-8'; '.js'='application/javascript; charset=utf-8'; '.mjs'='application/javascript; charset=utf-8'; '.json'='application/json; charset=utf-8'; '.png'='image/png'; '.jpg'='image/jpeg'; '.jpeg'='image/jpeg'; '.gif'='image/gif'; '.svg'='image/svg+xml'; '.ico'='image/x-icon'; '.webp'='image/webp'; '.woff'='font/woff'; '.woff2'='font/woff2'; '.txt'='text/plain; charset=utf-8'; '.map'='application/json'; '.wasm'='application/wasm'; '.mp4'='video/mp4' }
        $listener = [System.Net.HttpListener]::new()
        $listener.Prefixes.Add("http://localhost:$port/")
        $listener.Start()
        $rootFull = [IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
        while ($listener.IsListening) {
            try { $ctx = $listener.GetContext() } catch { break }
            try {
                $rel = [Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath).TrimStart('/')
                if ([string]::IsNullOrWhiteSpace($rel)) { $rel = 'index.html' }
                $path = [IO.Path]::GetFullPath((Join-Path $root ($rel -replace '/','\')))
                if (-not $path.StartsWith($rootFull,[StringComparison]::OrdinalIgnoreCase)) { $ctx.Response.StatusCode = 403 }
                elseif (Test-Path $path -PathType Container) {
                    $idx = Join-Path $path 'index.html'
                    if (Test-Path $idx -PathType Leaf) { $path = $idx } else { $ctx.Response.StatusCode = 404 }
                } elseif (-not (Test-Path $path -PathType Leaf)) { $ctx.Response.StatusCode = 404 }
                if ($ctx.Response.StatusCode -eq 200) {
                    $ext = [IO.Path]::GetExtension($path).ToLowerInvariant()
                    $ct = if ($mime.ContainsKey($ext)) { $mime[$ext] } else { 'application/octet-stream' }
                    $bytes = [IO.File]::ReadAllBytes($path)
                    $ctx.Response.ContentType = $ct
                    $ctx.Response.ContentLength64 = $bytes.Length
                    $ctx.Response.OutputStream.Write($bytes,0,$bytes.Length)
                }
                $ctx.Response.OutputStream.Close()
            } catch { try { $ctx.Response.StatusCode = 500; $ctx.Response.OutputStream.Close() } catch {} }
        }
    }
}

function Test-LocalHttp {
    param([string]$BaseUrl, [string]$LocalHtml, $Assets)
    $r = Invoke-Http -Url $BaseUrl
    if (-not $r.Ok) { Add-Result 'HTTP' 'GET /' 'FAIL' $r.Error; return }
    if ($r.Status -eq 200) { Add-Result 'HTTP' 'GET /' 'OK' ("200 - {0} ms - {1:N0} chars" -f $r.Ms, $r.Content.Length) }
    else { Add-Result 'HTTP' 'GET /' 'FAIL' ("Status: {0}" -f $r.Status) }
    if ($r.ContentType -match 'text/html') { Add-Result 'HTTP' 'Content-Type' 'OK' $r.ContentType }
    else { Add-Result 'HTTP' 'Content-Type' 'WARN' ("Expected text/html, got: " + $r.ContentType) }
    $lt = Get-HtmlTitle $LocalHtml; $st = Get-HtmlTitle $r.Content
    if ($lt -and $lt -eq $st) { Add-Result 'HTTP' 'Served version' 'OK' "title: $st" }
    else { Add-Result 'HTTP' 'Served version' 'WARN' 'Server returns different title - check root dir' }
    $bad = 0; $n = 0
    foreach ($a in $Assets) {
        if ($n -ge 40) { break }
        if (-not $a.Exists) { continue }
        $n++
        $u = $BaseUrl + (($a.Ref -split '[?#]')[0].TrimStart('/'))
        $ar = Invoke-Http -Url $u
        if (-not $ar.Ok -or $ar.Status -ne 200) { $bad++ }
    }
    if ($n -eq 0) { Add-Result 'HTTP' 'Local assets' 'INFO' 'No separate static files requested' }
    elseif ($bad -eq 0) { Add-Result 'HTTP' 'Local assets' 'OK' "All $n assets return 200" }
    else { Add-Result 'HTTP' 'Local assets' 'FAIL' "Unavailable: $bad of $n" }
    $nf = Invoke-Http -Url ($BaseUrl + '__ovs_missing__')
    if ($nf.Ok -and $nf.Status -eq 404) { Add-Result 'HTTP' '404 handling' 'OK' 'Missing path returns 404' }
    elseif ($nf.Ok) { Add-Result 'HTTP' '404 handling' 'WARN' ("Missing path returned {0}" -f $nf.Status) }
    else { Add-Result 'HTTP' '404 handling' 'WARN' 'Could not verify 404' }
}

function Test-RemoteEndpoint {
    param([string]$Url, [string]$Scope, [string]$LocalTitle, [bool]$Critical)
    if (-not $Url) { return }
    $r = Invoke-Http -Url $Url
    if ($r.Ok) {
        $detail = "{0} -> {1} - {2} ms" -f $Url, $r.Status, $r.Ms
        if ($r.Server) { $detail += " - server: " + $r.Server }
        Add-Result $Scope 'Reachability' 'OK' $detail
        if ($Scope -eq 'Internet') {
            if ($Url -match '^https://') { Add-Result $Scope 'HTTPS' 'OK' 'Certificate accepted, TLS OK' }
            else { Add-Result $Scope 'HTTPS' 'WARN' 'URL is not HTTPS' }
            $rt = Get-HtmlTitle $r.Content
            if ($LocalTitle -and $rt -eq $LocalTitle) { Add-Result $Scope 'Version' 'OK' 'Matches local page' }
            elseif ($LocalTitle) { Add-Result 'Scope' 'Version' 'WARN' "Remote title differs: '$rt' - redeploy" }
        }
    } else {
        $st = if ($Critical) { 'FAIL' } else { 'WARN' }
        Add-Result $Scope 'Reachability' $st ("{0} -> {1}" -f $Url, $r.Error)
    }
}

function Write-Report {
    param([string]$SiteRoot, [string]$BaseUrl, [string]$ProjectRoot)
    $logs = Join-Path $ProjectRoot 'logs'
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $file = Join-Path $logs 'site-test-report.html'
    $rows = foreach ($r in $Results) {
        "<tr class='$($r.Status.ToLower())'><td>$([System.Net.WebUtility]::HtmlEncode($r.Scope))</td><td>$([System.Net.WebUtility]::HtmlEncode($r.Name))</td><td class='st'>$($r.Status)</td><td>$([System.Net.WebUtility]::HtmlEncode($r.Detail))</td></tr>"
    }
    $ok   = @($Results | Where-Object { $_.Status -eq 'OK' }).Count
    $warn = @($Results | Where-Object { $_.Status -eq 'WARN' }).Count
    $fail = @($Results | Where-Object { $_.Status -eq 'FAIL' }).Count
    $sum = "<span class='ok'>$ok OK</span> / <span class='warn'>$warn WARN</span> / <span class='fail'>$fail FAIL</span>"
    $meta = Get-Date -Format 'dd.MM.yyyy HH:mm:ss'
    $tpl = @'
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>OVS Site Test Report</title>
<style>
body{background:#0a1120;color:#e7eef9;font:14px/1.55 "Segoe UI",system-ui,sans-serif;margin:0;padding:32px}
h1{font-size:22px;margin:0 0 6px}.meta{color:#90a1bc;font-size:12.5px;margin:4px 0}
.sum{font-size:16px;font-weight:800;margin:16px 0}
.ok{color:#34d399}.warn{color:#fbbf24}.fail{color:#fb4d6d}
table{width:100%;border-collapse:collapse;margin-top:10px;background:#0e1728;border:1px solid #1e2c47}
th,td{padding:10px 14px;text-align:left;border-bottom:1px solid #1e2c47;font-size:13px;vertical-align:top}
th{background:#121e33;color:#90a1bc;text-transform:uppercase;font-size:11px;letter-spacing:1px}
td.st{font-weight:900;font-family:Consolas,monospace}
tr.ok td.st{color:#34d399}tr.warn td.st{color:#fbbf24}tr.fail td.st{color:#fb4d6d}tr.info td.st{color:#22d3ee}
tr:hover td{background:#121e33}
</style></head><body>
<h1>OVS - Web Panel Test Report</h1>
<p class="meta">__META__</p>
<div class="sum">__SUM__</div>
<table><thead><tr><th>Scope</th><th>Check</th><th>Status</th><th>Details</th></tr></thead><tbody>__ROWS__</tbody></table>
<p class="meta">Root: __ROOT__ - Local URL: __URL__</p>
</body></html>
'@
    $out = $tpl.Replace('__META__',$meta).Replace('__SUM__',$sum).Replace('__ROWS__',($rows -join "`n")).Replace('__ROOT__',[System.Net.WebUtility]::HtmlEncode($SiteRoot)).Replace('__URL__',$(if($BaseUrl){$BaseUrl}else{'-'}))
    [IO.File]::WriteAllText($file, $out, [Text.UTF8Encoding]::new($false))
    return $file
}

# ================= MAIN =================
Write-Host ''
Write-Host '=== OVS site smoke test v4 ===' -ForegroundColor Cyan
$ProjectRoot = Resolve-ProjectRoot
Add-Result 'Files' 'Project root' 'OK' $ProjectRoot
$SiteRoot = Resolve-SiteRoot -ProjectRoot $ProjectRoot
if (-not $SiteRoot) {
    Add-Result 'Files' 'Site root' 'FAIL' 'index.html not found in frontend/, frontend/dist, frontend/build, docs/ or project root'
    Write-Report -SiteRoot $ProjectRoot -BaseUrl $null -ProjectRoot $ProjectRoot | Out-Null
    exit 1
}
Add-Result 'Files' 'Site root' 'OK' $SiteRoot
$html = Test-StaticSite -SiteRoot $SiteRoot
$scan = Get-LocalAssets -Html $html -SiteRoot $SiteRoot
$assets = $scan.List
if ($scan.Dynamic -gt 0) { Add-Result 'Files' 'Dynamic URLs' 'INFO' ("JS template URLs skipped: {0}" -f $scan.Dynamic) }

# Robust API counting (fixed null reference issue)
$apiRefs = $scan.Api
if ($html) {
    $apiRefs += ([regex]::Matches($html, '/api/')).Count
}
Get-ChildItem $SiteRoot -Filter *.js -Recurse -ErrorAction SilentlyContinue | Select-Object -First 20 | ForEach-Object {
    try {
        $c = Get-Content $_.FullName -Raw -Encoding UTF8 -ErrorAction Stop
        if ($c) {
            $apiRefs += ([regex]::Matches($c, '/api/')).Count
        }
    } catch {}
}
if ($apiRefs -gt 0) { Add-Result 'Files' 'Backend API' 'INFO' ("API references: {0}. Run scripts/dev-server.py to proxy /api to the backend" -f $apiRefs) }

$missing = @($assets | Where-Object { -not $_.Exists })
$missFiles = @($missing | Where-Object { [IO.Path]::GetExtension($_.Full) })
$missRoutes = @($missing | Where-Object { -not [IO.Path]::GetExtension($_.Full) })
if ($assets.Count -eq 0) { Add-Result 'Files' 'Local assets' 'INFO' 'Self-contained page' }
elseif ($missFiles.Count) { Add-Result 'Files' 'Local assets' 'FAIL' (($missFiles | Select-Object -First 6 | ForEach-Object Ref) -join ', ') }
elseif ($missRoutes.Count) { Add-Result 'Files' 'Local assets' 'WARN' ("Extensionless paths (routes?): " + (($missRoutes | Select-Object -First 4 | ForEach-Object Ref) -join ', ')) }
else { Add-Result 'Files' 'Local assets' 'OK' ("All local references exist: {0}" -f $assets.Count) }

$BaseUrl = $null; $proc = $null; $job = $null; $started = $false
try {
    if (-not $NoServe) {
        $Port = Get-FreePort -Start $Port
        $bind = if ($Lan) { '0.0.0.0' } else { '127.0.0.1' }
        $BaseUrl = "http://127.0.0.1:$Port/"
        $py = Get-PythonCommand
        if ($py) {
            $pyArgs = @()
            if ($py.Count -gt 1) { $pyArgs += $py[1..($py.Count-1)] }
            $pyArgs += @('-m','http.server',"$Port",'--bind',$bind)
            $logDir = Join-Path $ProjectRoot 'logs'
            New-Item -ItemType Directory -Path $logDir -Force | Out-Null
            $logOut = Join-Path $logDir 'dev-server-out.log'
            $logErr = Join-Path $logDir 'dev-server-err.log'
            $proc = Start-Process -FilePath $py[0] -ArgumentList $pyArgs -WorkingDirectory $SiteRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $logOut -RedirectStandardError $logErr
            Start-Sleep -Milliseconds 900
            if ($proc.HasExited) {
                $tail = (Get-Content $logErr -Tail 3 -ErrorAction SilentlyContinue) -join ' '
                Add-Result 'HTTP' 'Python server' 'WARN' ("Exited early: $tail")
                $proc = $null
            } else {
                Add-Result 'HTTP' 'Python server' 'OK' "http.server - port $Port - bind $bind"
                if (Wait-Http -Url $BaseUrl) { $started = $true }
                else {
                    Add-Result 'HTTP' 'Python server' 'WARN' 'No HTTP response, falling back to HttpListener'
                    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
                    $proc = $null
                }
            }
        }
        if (-not $started) {
            if ($Lan) { Add-Result 'HTTP' 'LAN mode' 'WARN' 'Python required for -Lan; using localhost' }
            $job = Start-HttpListenerJob -SiteRoot $SiteRoot -Port $Port
            if (Wait-Http -Url $BaseUrl) {
                $started = $true
                Add-Result 'HTTP' 'Fallback server' 'OK' "PowerShell HttpListener - port $Port"
            } else {
                Add-Result 'HTTP' 'Server start' 'FAIL' 'No server responded within timeout'
            }
        }
        if ($started) {
            Test-LocalHttp -BaseUrl $BaseUrl -LocalHtml $html -Assets $assets
            if ($Lan -and $py) {
                $ips = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
                    Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
                    Select-Object -ExpandProperty IPAddress
                if ($ips) { Add-Result 'HTTP' 'LAN URL' 'INFO' ("From phone on same Wi-Fi: " + (($ips | ForEach-Object { "http://$_`:$Port/" }) -join '  or  ')) }
            }
            if (-not $NoBrowser -and -not $Once) { Start-Process $BaseUrl }
        }
    }

    if ($BackendUrl) { Test-RemoteEndpoint -Url $BackendUrl -Scope 'Backend' -LocalTitle $null -Critical $false }
    Test-RemoteEndpoint -Url $DeviceUrl -Scope 'Device' -LocalTitle $null -Critical $false
    Test-RemoteEndpoint -Url $PublicUrl -Scope 'Internet' -LocalTitle (Get-HtmlTitle $html) -Critical $true

    $report = Write-Report -SiteRoot $SiteRoot -BaseUrl $(if($started){$BaseUrl}else{$null}) -ProjectRoot $ProjectRoot
    $ok   = @($Results | Where-Object { $_.Status -eq 'OK' }).Count
    $warn = @($Results | Where-Object { $_.Status -eq 'WARN' }).Count
    $fail = @($Results | Where-Object { $_.Status -eq 'FAIL' }).Count
    Write-Host ''
    $sumColor = if ($fail) { 'Red' } elseif ($warn) { 'Yellow' } else { 'Green' }
    Write-Host ("Result: {0} OK / {1} WARN / {2} FAIL" -f $ok,$warn,$fail) -ForegroundColor $sumColor
    Write-Host "Report: $report" -ForegroundColor Cyan

    if ($started -and -not $Once) {
        Write-Host ''
        Write-Host "Server running: $BaseUrl" -ForegroundColor Green
        Write-Host 'Browser checks: F12 -> Console (no red errors), Ctrl+Shift+M (mobile layout).' -ForegroundColor DarkGray
        Read-Host 'Press Enter to stop the server'
    }
}
finally {
    if ($proc) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    if ($job)  { Stop-Job $job -ErrorAction SilentlyContinue; Remove-Job $job -Force -ErrorAction SilentlyContinue }
    if ($script:Http) { $script:Http.Dispose() }
}
$failCount = @($Results | Where-Object { $_.Status -eq 'FAIL' }).Count
exit $failCount