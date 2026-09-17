# K-UI startup recovery — 2026-09-17

The maintainer reports a black screen with a movable cursor after the splash
using a freshly replaced DS directory from the unpublished master build
`54b1c9c`. The hardware cause has not yet been established.

The maintainer confirms that no VMU is connected, so VMU settings are excluded
from this incident. The DS directory was removed before installing the ZIP.

## Drop-in startup recovery script

Back up `DS/lua/startup.lua`, then replace just that file with the current
`resources/lua/startup.lua`. No new core or disc is required for this script.
Do not replace or delete the rest of DS when applying the small patch.

The script retries `Launch App` if a selected startup app cannot open. It
records module loading, app registration, the selected app, and Lua failures
in `DS/kui-startup.log` when that directory is writable. The log is replaced
on each boot, and each step is flushed. Logging failure does not prevent boot.
Unrecoverable script failures reveal the console and propagate the error.

After one attempted boot, collect `DS/kui-startup.log` and, if present,
`DS/kui-memory.log`. A photo of an error console is also useful. If no startup
log is created, report that: the selected root or script may mean this file never
executed, or the selected media may not be writable. The absence of a log alone
does not prove either explanation.

The accompanying C fixes take effect in a future rebuilt core. They reveal
startup Lua errors even when the script itself cannot load, and make `lfs.dir`
report an invalid directory handle rather than treating it as an empty folder.
The drop-in script does not replace the core and cannot supply these C fixes.

To undo the script patch, restore the backed-up `DS/lua/startup.lua`.

## Evidence and scope

The actual full-build artifacts were compared:

| Build | Commit | Actions run |
| --- | --- | --- |
| Previously tested | `62a5fbc` | `35180819886` |
| Reported regression | `54b1c9c` | `35224435502` |

Both contain the same 516 file paths. Launcher module/XML/assets, startup Lua,
fonts, and Tsunami are byte-identical. Runtime source changes after the tested
commit are branding strings. The six Lua binding modules have reordered type
registrations from tolua's unordered generator traversal, with identical
registered type sets. Build flags and dependency revisions also match.
This comparison does not establish that the two cores behave identically on
hardware or explain the console failure.

Two concrete silent-failure paths were found: `InitDS` ignored startup Lua
errors, and the standard script ignored a failed `OpenApp`. The directory
binding also compared an unsigned handle with zero, so failure detection was
ineffective. The recovery changes address these paths; a console retest is
still needed to establish the reported regression's cause and resolution.
