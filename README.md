# Profiles for Claude Desktop

[![CI](https://github.com/kkrysztofczyk/kkr-claude-profiles/actions/workflows/ci.yml/badge.svg)](https://github.com/kkrysztofczyk/kkr-claude-profiles/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Windows](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D4)](#requirements)

Run Claude Desktop under separate profiles, so two or more accounts can be signed in
at the same time, each in its own window.

*[Polska wersja tego dokumentu →](README.pl.md)*

> **Unofficial project.** Not built, endorsed, or reviewed by Anthropic. "Claude" and
> "Anthropic" are trademarks of Anthropic PBC, used here descriptively — to say what
> this program works with. This repository contains no Anthropic code or artwork; the
> icon is original. Licensed under the [MIT License](LICENSE).

Claude Desktop ships as an MSIX package and allows a single instance per user data
directory. This launcher starts it with a different `--user-data-dir` per account, so
a personal and a work subscription can run side by side.

## Quick start

1. Download **`claude-profiles.exe`** from the [latest release](https://github.com/kkrysztofczyk/kkr-claude-profiles/releases/latest)
2. Put it wherever you like — the desktop is fine
3. Double-click it

That is the whole setup. One self-contained file, no installer, no runtime, no
dependencies, nothing written to the registry. The picker opens; pick an account.

Right-click → **Pin to taskbar** if you want it one click away. To remove the program,
delete the file.

## Windows will warn you — that is expected

The first launch shows a blue full-screen dialog: **"Windows protected your PC"**, saying
SmartScreen prevented an unrecognized app from starting. To run it anyway:

**More info** → **Run anyway**

You may also need to lift the download block: right-click the file → **Properties** →
tick **Unblock** at the bottom of the General tab → **OK**.

This happens because the binary is **not code-signed**. A signing certificate costs a few
hundred euros a year, and even a signed app needs download volume before SmartScreen stops
flagging it. Nothing about the warning is specific to this program — every unsigned
executable downloaded from the internet gets it.

If that is not good enough for you, do not click through it. Either [build the program
yourself](#build) from source in one command, or read [Verify it
yourself](#verify-it-yourself) first. Both are better answers than trusting a stranger's
binary, and neither takes long.

## Verify it yourself

You do not have to take the binary on trust. Every release is built by GitHub Actions
from the tagged commit, so it is reproducible from public source, and the checksum is
published alongside it:

```powershell
Get-FileHash claude-profiles.exe -Algorithm SHA256
```

Compare the result with `SHA256SUMS.txt` attached to the same release.

The program is about 1200 lines of C++ across three files, so reading it end to end is
realistic.

**Or let an AI read it for you.** This works in any assistant — ChatGPT, Claude, Gemini,
Copilot, whichever you already use. Copy the prompt below and paste it in:

```
I want to install this Windows program. Read its source and tell me honestly whether
it is safe to run:

https://github.com/kkrysztofczyk/kkr-claude-profiles

Answer in plain language:
1. What does it actually do when I double-click it?
2. Does it send anything over the internet — telemetry, analytics, phoning home?
3. Can it read my passwords, tokens, conversations or any files outside its own folder?
4. What does it write to my disk and where? Can I remove it completely?
5. Does the code match what the README claims, or is something undisclosed?
6. What are the real risks, and would you install it on your own machine?

Be blunt. If something looks suspicious, say so. If you cannot open the link, tell me
and I will paste the source files instead.
```

For the record, so you can check the answer you get against what this project claims: the
program resolves where Claude Desktop is installed, starts it with a `--user-data-dir`
switch, appends one line to its own log file, and does nothing else. It opens no network
connections — it does not even link a networking library. It reads no credentials. It
creates profile directories and never reads what Claude puts inside them.

If an assistant tells you something that contradicts this, trust the assistant and
[open an issue](https://github.com/kkrysztofczyk/kkr-claude-profiles/issues) — either the
code or this README is wrong, and both are worth fixing.

## Usage

```
claude-profiles.exe              account picker
claude-profiles.exe 1            account 1 - the default profile
claude-profiles.exe 2            account 2 - profile Claude-konto2
claude-profiles.exe 3            account 3 - profile Claude-konto3
claude-profiles.exe --profile X  any profile directory named X
```

Started with no arguments it opens its own picker window, so a single desktop shortcut
handles every account. The picker lists the default profile, every profile it finds on
disk, and an entry that creates the next one — there is no configuration file to edit.

## Requirements

- Windows 10 or 11
- Claude Desktop installed from the Microsoft Store (the MSIX package)
- Visual Studio with the **Desktop development with C++** workload — only to build

## Build

```bat
build.cmd
```

Produces `build\claude-profiles.exe` — x64, statically linked CRT, no runtime
dependencies. The build treats warnings as errors (`/W4 /WX /permissive-`), so it either
compiles cleanly or not at all.

The script locates MSVC by probing the known Visual Studio install paths; no `vswhere`
and no developer command prompt required. Some Visual Studio versions print
`'vswhere.exe' is not recognized` to stderr from their own `vcvars64.bat` — that is noise
from Microsoft's scripts, not from this project, and compilation succeeds regardless.

The icon is generated by `tools\make-icon.ps1` into `res\launcher.ico`. Run it only when
changing the icon design; the result is committed.

## Install from source

```bat
start.cmd
```

Builds, installs, and launches in one step. The individual steps:

```bat
build.cmd      :: compile
install.cmd    :: copy to %LOCALAPPDATA%\KKr\ClaudeProfiles + desktop shortcut
uninstall.cmd  :: remove the program and its shortcuts
```

Uninstalling leaves **account profiles untouched** — they hold signed-in sessions, so
deleting them would mean signing in again. Remove them by hand if you really want them
gone.

## Exit codes

| Code | Meaning |
|------|---------|
| 0    | launched, or the picker was dismissed |
| 1    | error — details are shown in a dialog |
| 2    | invalid arguments |

## Log

Every run appends to `%LOCALAPPDATA%\KKr\ClaudeProfiles\claude-profiles.log` (UTF-8, one
timestamped line per event): the arguments and mode, the resolved package path, which
launch strategy worked or which error code it failed with, and whether the process
survived and the profile directory appeared.

```bat
type %LOCALAPPDATA%\KKr\ClaudeProfiles\claude-profiles.log
```

This exists so that diagnosing a failed launch never depends on reproducing it. Log
writes can never block a launch — any failure to write is silently ignored. The file
grows without bound, but a run costs a few short lines; deleting it at any time is safe,
and `uninstall.cmd` removes it along with everything else.

The picker window title shows the build date and time, which makes it obvious whether the
installed copy is actually the latest build.

## Design notes

**The path to `Claude.exe` is resolved at runtime.** Claude Desktop is an MSIX package,
so its install directory contains a version number
(`C:\Program Files\WindowsApps\Claude_<version>_x64__<hash>`) and changes with every
update. Hardcoding it would break the shortcut after the first update.

The primary source is the package model (`GetPackagesByPackageFamily` /
`GetPackagePathByFullName` from `appmodel.h`), which works regardless of whether the
process runs inside an MSIX container. The `claude://` protocol registration is only a
fallback: that registry key exists *only* inside the app's container, so a launcher
started from a desktop shortcut cannot see it. Considered and rejected:

- `FindPackagesByPackageFamilyName` — not declared in the Windows SDK 10.0.26100
  headers, despite existing as a `kernel32` export.
- Globbing `WindowsApps\Claude_*` — the ACL on `WindowsApps` blocks directory
  enumeration; `FindFirstFile` returns access denied.
- `AppModel\Repository` registry keys — not present on the test machine.

**Account 1 starts differently from the others.** The default profile is launched via
`IApplicationActivationManager::ActivateApplication`, not `CreateProcess`, because only
package activation preserves MSIX identity — which system notifications and `claude://`
links depend on. Additional profiles must go through `CreateProcess`, since that is the
only way to pass `--user-data-dir`. See [FINDINGS.md](FINDINGS.md) for why the obvious
alternative (passing arguments to `ActivateApplication`) does not work.

**No instance synchronization of its own.** Electron holds a single-instance lock keyed
by the user data directory, so launching the same profile twice just raises the existing
window. A separate mutex would add nothing.

**The picker is custom-drawn; error dialogs are not.** A `TaskDialog` with a system icon
reads like an error message, which is misleading for a start screen — hence the custom
window with cards, dark mode support, and Windows 11 rounded corners. Real errors still
use `TaskDialog`, where a system-style message is appropriate. If comctl32 v6 were
unavailable, every error dialog falls back to `MessageBox`.

**The icon deliberately does not resemble the Claude mark.** It is Anthropic's trademark,
and a lookalike would imply an affiliation that does not exist. The practical reason
matters more day to day: an icon resembling the original would be indistinguishable from
the app itself in the taskbar. The glyph shows two overlapping windows — what the program
actually does.

## Limitations

**Deep links always open account 1.** The `claude://` protocol is registered system-wide
against the bare application, with no `--user-data-dir`. All deep links and OAuth
callbacks therefore reach the default profile. The practical consequence: signing in with
"Continue with Google" does not work in a secondary instance — use email plus a one-time
code, which happens entirely inside the app window. This is a system-level constraint:
two processes cannot share one protocol registration.

**Profile directories are not where `--user-data-dir` points.** Claude runs inside an
MSIX container, so its writes to `%APPDATA%` are redirected into the package's
LocalCache. A profile passed as `%APPDATA%\Claude-konto2` actually lands in
`%LOCALAPPDATA%\Packages\<PackageFamilyName>\LocalCache\Roaming\Claude-konto2`. The
launcher checks both locations — see [FINDINGS.md](FINDINGS.md).

**Accounts do not share conversation history.** That lives server-side, per account.
Locally, `%USERPROFILE%\.claude` stays shared (Claude Code sessions, memory, projects),
because it sits outside Electron's data directories.

**The binary is not code-signed**, so SmartScreen warns on a freshly downloaded copy.
Building it yourself avoids the warning.

**Tied to one package family name.** The launcher targets `Claude_pzs8sxrjxfjjc`; if
Anthropic ever republishes under a different identity, the constant in
[src/main.cpp](src/main.cpp) needs updating.

## Findings

[FINDINGS.md](FINDINGS.md) documents what came out of building this — MSIX path
virtualization, the undocumented value of `PROC_THREAD_ATTRIBUTE_PACKAGE_FULL_NAME`, and
why `ActivateApplication` arguments never reach an Electron app. If you are trying to run
a second instance of any Store-distributed Electron application, that file is likely more
useful than this launcher.

## A note on the code

Source comments are in Polish; the documentation is in English and Polish. The comments
explain *why* each decision was made rather than what the code does, and they were
written alongside the debugging that produced them.

---

One of the `kkr-*` tools by [@kkrysztofczyk](https://github.com/kkrysztofczyk).
