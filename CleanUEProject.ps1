[CmdletBinding()]
param([switch]$Preview)

# Run manually with CleanUEProject.cmd. -Preview only lists the cleanup plan.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = (Resolve-Path -LiteralPath $PSScriptRoot).Path.TrimEnd('\')
$expected = 'C:\Users\fujin\Desktop\UPENN\Term3\CIS5680\ALPHA\BlastDelivery'
if ($project -ne $expected) { throw 'This cleanup is restricted to the audited BlastDelivery project.' }

function Assert-PlainPath([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked path refused: $Path" }
    if ($item.PSIsContainer) {
        $links = @(Get-ChildItem -LiteralPath $Path -Recurse -Force | Where-Object {
            $_.Attributes -band [IO.FileAttributes]::ReparsePoint
        })
        if ($links.Count) { throw "Nested linked path refused: $Path" }
    }
}
function Get-Bytes([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if (!$item.PSIsContainer) { return [long]$item.Length }
    return [long](Get-ChildItem -LiteralPath $Path -Recurse -File -Force | Measure-Object Length -Sum).Sum
}

# Never stop applications or discard unsaved editor work.
if (!$Preview) {
    $busy = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -in @('UnrealEditor.exe','UnrealEditor-Cmd.exe','zenserver.exe',
            'UnrealBuildTool.exe','UnrealHeaderTool.exe','UbaHost.exe','UbaAgent.exe','ShaderCompileWorker.exe') -or
        ($_.Name -eq 'dotnet.exe' -and $_.CommandLine -match 'UnrealBuildTool')
    })
    if ($busy.Count) { throw 'Save and close Unreal Editor and wait for UE build/cache workers to exit, then run again. Unity can stay open.' }
}

$old = 'C:\Users\fujin\Downloads\BlastDelivery-main'
$archive = 'C:\Users\fujin\Downloads\BlastDelivery-main.zip'
if ((Test-Path -LiteralPath $archive) -and
    (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne
        '6BC700F218EA73B14D28026A85A77F527455CAC9086A74000C8C5DE727DBD528') {
    throw 'The old-project archive has changed since the audit; nothing will be deleted.'
}
$targets = @(
    (Join-Path $project 'Intermediate'),
    (Join-Path $project 'Saved\Crashes'),
    (Join-Path $project 'Saved\ShaderDebugInfo'),
    'C:\Users\fujin\AppData\Local\UnrealEngine\Common\DerivedDataCache',
    'C:\Users\fujin\AppData\Local\UnrealEngine\Common\Zen\Data',
    'C:\Users\fujin\AppData\Local\UnrealEngine\5.6\Intermediate',
    'C:\Users\fujin\AppData\Local\UnrealEngine\5.6\DerivedDataCache'
)

# Delete the old downloaded project only if every authored file still matches
# its original archive. New or edited source/assets stop the entire operation.
if (Test-Path -LiteralPath $old) {
    if (!(Test-Path -LiteralPath $archive)) { throw 'The original archive is missing; old-project verification cannot proceed.' }
    Assert-PlainPath $old
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entries = @{}
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName.EndsWith('/')) { continue }
            $relative = $entry.FullName.Substring($entry.FullName.IndexOf('/') + 1)
            $entries[$relative] = $entry
        }
        foreach ($file in Get-ChildItem -LiteralPath $old -Recurse -File -Force) {
            $relative = $file.FullName.Substring($old.Length + 1).Replace('\','/')
            if ($relative -match '^(Binaries|Intermediate|DerivedDataCache|Saved)/') { continue }
            if (!$entries.ContainsKey($relative)) { throw "Unexpected file in old project: $relative" }
            $stream = $entries[$relative].Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
            finally { $stream.Dispose(); $sha.Dispose() }
            if ($hash -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) {
                throw "Old project was edited; not deleting: $relative"
            }
        }
    } finally { $zip.Dispose() }
    $targets += $old
}
if (Test-Path -LiteralPath $archive) { $targets += $archive }

# Remove superseded test output, keeping the latest screenshots and passing reports.
$reports = Join-Path $project 'Saved\Automation'
if (Test-Path -LiteralPath $reports) {
    $targets += @(Get-ChildItem -LiteralPath $reports -Directory | Where-Object {
        $_.Name -notin @('AlphaShots','ButtonFeedback','UnityAlignment-TutorialVerified')
    } | Select-Object -ExpandProperty FullName)
}
$logs = Join-Path $project 'Saved\Logs'
if (Test-Path -LiteralPath $logs) {
    $targets += @(Get-ChildItem -LiteralPath $logs -File | Where-Object {
        $_.Name -notin @('BlastDelivery568.log','ButtonFeedback-Build.log',
            'ButtonFeedback-Test.log','UnityAlignment-TutorialVerified.log','CleanupLatest.txt')
    } | Select-Object -ExpandProperty FullName)
}

# Resolve and validate ALL targets before deleting any. Never follow links.
$allowed = @($targets | ForEach-Object { [IO.Path]::GetFullPath($_) })
$plan = @()
foreach ($target in $targets) {
    if (!(Test-Path -LiteralPath $target)) { continue }
    $resolved = (Resolve-Path -LiteralPath $target).Path
    if ($resolved -notin $allowed) { throw "Unexpected resolved target: $resolved" }
    Assert-PlainPath $resolved
    $plan += [PSCustomObject]@{ Path=$resolved; Bytes=(Get-Bytes $resolved) }
}
$total = [long]($plan | Measure-Object Bytes -Sum).Sum
Write-Host 'Preserved: latest UE source, assets, config, Git history, compiled binaries, settings and autosaves.'
Write-Host 'Preserved: ALL Unity projects/builds/caches, UE 5.8 installation and unrelated projects.'
$plan | Select-Object @{Name='GiB';Expression={[math]::Round($_.Bytes/1GB,3)}}, Path | Format-Table -AutoSize
Write-Host ('Expected cleanup: {0:N2} GiB' -f ($total/1GB))
if ($Preview) { Write-Host 'PREVIEW ONLY: nothing deleted.'; return }

# Hash the current authored files and working binaries before/after cleanup.
$protected = @()
foreach ($name in @('Source','Content','Config','Scripts','Binaries','.git')) {
    $path = Join-Path $project $name
    if (Test-Path -LiteralPath $path) { $protected += @(Get-ChildItem -LiteralPath $path -Recurse -File -Force) }
}
foreach ($name in @('BlastDelivery568.uproject','OpenAlphaEditor.cmd','.gitignore','.vsconfig')) {
    $protected += Get-Item -LiteralPath (Join-Path $project $name)
}
$hashes = @($protected | ForEach-Object { Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256 })
$freeBefore = (Get-PSDrive C).Free
foreach ($target in $plan) {
    Write-Host ('Removing: ' + $target.Path)
    Remove-Item -LiteralPath $target.Path -Recurse -Force
}
foreach ($file in $hashes) {
    if (!(Test-Path -LiteralPath $file.Path) -or
        (Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne $file.Hash) {
        throw ('Protected-file verification failed: ' + $file.Path)
    }
}
$freeAfter = (Get-PSDrive C).Free
$projectBytes = Get-Bytes $project
$summary = @(
    ('Completed: ' + (Get-Date -Format s)),
    ('Deleted files: {0:N2} GiB' -f ($total/1GB)),
    ('Observed C: free-space change: {0:N2} GiB' -f (($freeAfter-$freeBefore)/1GB)),
    ('Current UE project: {0:N1} MiB' -f ($projectBytes/1MB)),
    ('C: free space: {0:N2} GiB' -f ($freeAfter/1GB)),
    ('Verified unchanged protected files: ' + $hashes.Count),
    'Unity was not touched. UE can open using the existing compiled binaries.',
    'Some caches will regenerate on later loads/builds; no engine reinstall is needed.'
)
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$summary | Set-Content -LiteralPath (Join-Path $logs 'CleanupLatest.txt') -Encoding UTF8
$summary | ForEach-Object { Write-Host $_ }
