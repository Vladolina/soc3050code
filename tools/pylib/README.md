# tools/pylib — Python packages the course vendors

So a clone runs the PC-side tools with **no `pip install`**, the same promise
the vendored compilers keep. Tools that need a package here put this folder on
`sys.path` themselves (see the top of `tools/station/station.py`).

| Package | Version | From | Why |
|---|---|---|---|
| `serial/` | pyserial 3.5 | `pyserial-3.5-py2.py3-none-any.whl` from PyPI, SHA-256 `c4451db6ba391ca6ca299fb3ec7bae67a5c55dde170964c7a14ceefec02f2cf0` (checked by pip against the index on download, 2026-11-03) | the station's serial link: `rfc2217://` for Wokwi, `COMn` for a real board, `socket://` for `fakeboard.py` |

Only the `serial/` package was unpacked from the wheel; its `.dist-info`
metadata was not. pyserial is © 2001-2020 Chris Liechti, BSD-3-Clause — the
licence text is in the header of every file under `serial/`.

Pure Python, so one copy serves every platform. To update: download the new
wheel, check its hash against PyPI, replace `serial/`, and update this table.
