# station.ps1 - start tools/station/station.py with whichever Python works.
#
# The station is the one part of the course that needs something not in the
# repository: Python 3 with tkinter.  On a student PC "python" may be the
# Microsoft Store placeholder (it opens the Store, or fails with exit 9009),
# or missing from PATH while the "py" launcher works.  So each candidate is
# tried for real - it must run and import tkinter - before it is used.
#
# Used by the VS Code task "ARM: Station"; extra arguments pass through:
#     powershell -File tools\cli\station.ps1 --port COM5

$station = Join-Path $PSScriptRoot "..\station\station.py"
$candidates = @(@("py", "-3"), @("python"), @("python3"))

foreach ($c in $candidates) {
    $exe = $c[0]
    $pre = @($c | Select-Object -Skip 1)
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { continue }
    $ver = & $exe @pre -c "import sys, tkinter; print(sys.version.split()[0])" 2>$null
    if ($LASTEXITCODE -eq 0 -and $ver) {
        Write-Host "station: Python $ver ($exe $pre)"
        & $exe @pre $station @args
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "The station needs Python 3 with tkinter, and none was found." -ForegroundColor Yellow
Write-Host "Install it from https://www.python.org/downloads/ (tkinter is included;"
Write-Host "tick 'Add python.exe to PATH'), then run this task again."
Write-Host "Nothing else is needed: the serial library is in tools\pylib."
exit 1
