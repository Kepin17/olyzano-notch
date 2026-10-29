# Memory of Olyzano Notch at rest, from Get-Process: the working set is the figure to watch (the pages actually in RAM);
# the private bytes are what the program owns outright. Both are printed, plus the thread count, at 10, 25 and 40 seconds.
#   scripts\memcheck.ps1 [-Bin <path>] [-Variants default low]        "low" is what Low memory mode does (software renderer)
# Example:  powershell -ExecutionPolicy Bypass -File scripts\memcheck.ps1 -Bin build\notch.exe -Variants default,low
#
# Windows has no throw-away profile (QStandardPaths ignores the LOCALAPPDATA environment variable), so this uses your real
# %LOCALAPPDATA%\notch settings. CLOSE ANY RUNNING NOTCH FIRST: only one copy may run, otherwise the test copy exits at once.
param(
    [string]$Bin = "build\notch.exe",
    [string[]]$Variants = @("default", "low")
)

foreach ($v in $Variants) {
    switch ($v) {
        "default" { Remove-Item Env:QT_QUICK_BACKEND -ErrorAction SilentlyContinue }
        "low"     { $env:QT_QUICK_BACKEND = "software" }  # what Low memory mode sets before Qt starts
        default   { Write-Host "unknown variant $v"; continue }
    }

    try {
        $p = Start-Process -FilePath $Bin -PassThru
    } catch {
        Write-Host "$v: could not start $Bin ($($_.Exception.Message))"
        continue
    }
    Start-Sleep -Seconds 2
    $p.Refresh()
    if ($p.HasExited) {
        Write-Host "$v: the program stopped right away (is another Notch running? only one may)"
        continue
    }

    foreach ($t in 10, 25, 40) {
        Start-Sleep -Seconds $(if ($t -eq 10) { 10 } else { 15 })
        $p.Refresh()
        if ($p.HasExited) { Write-Host "$v: the program stopped at t=$($t)s"; break }
        $ws   = [math]::Round($p.WorkingSet64 / 1MB)
        $priv = [math]::Round($p.PrivateMemorySize64 / 1MB)
        Write-Host ("{0,-9} t={1,2}s  WS {2,4} MB  private {3,4} MB  threads {4}" -f $v, $t, $ws, $priv, $p.Threads.Count)
    }
    if (-not $p.HasExited) { Stop-Process -Id $p.Id }
    Start-Sleep -Seconds 2
}
Remove-Item Env:QT_QUICK_BACKEND -ErrorAction SilentlyContinue
