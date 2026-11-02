# station.ps1 - start tools/station/station.py in the course's uv environment.
#
# The repository root has pyproject.toml, uv.lock and .python-version: uv
# fetches Python 3.12 (with the tkinter the station needs) and pyserial
# itself.  "uv run" syncs the environment before it runs, so this works even
# if nobody ran "uv sync" first.
#
# Used by the VS Code task "ARM: Station"; extra arguments pass through:
#     powershell -File tools\cli\station.ps1 --port COM5

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$station = Join-Path $root "tools\station\station.py"

if (Get-Command uv -ErrorAction SilentlyContinue) {
    & uv run --project $root python $station @args
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "The station runs in the course's uv environment, and uv was not found." -ForegroundColor Yellow
Write-Host "Install uv once (a new terminal is needed afterwards):"
Write-Host "    powershell -ExecutionPolicy ByPass -c `"irm https://astral.sh/uv/install.ps1 | iex`""
Write-Host "then, at the repository root:"
Write-Host "    uv sync"
Write-Host "and run this task again."
exit 1
