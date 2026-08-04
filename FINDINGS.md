# Findings

Things that surfaced while building this launcher, and that are documented neither in
Microsoft's documentation nor anywhere search engines could reach. Written down because
anyone trying to run a second instance of an Electron application distributed as an MSIX
package from the Microsoft Store will hit exactly the same walls.

Each finding states what happened, what turned out to be true, how it was verified, and
what follows from it. Where the verification is indirect, that is said explicitly —
several of these have no authoritative source and rest entirely on observed behaviour.

---

## 1. An app in an MSIX container has its `%APPDATA%` redirected

**This one cost two debugging sessions and one confident, wrong diagnosis.**

The launcher passes `--user-data-dir="%APPDATA%\Claude-konto2"` and then checks whether
that directory appeared, as proof that the second instance really started. The directory
**never** appeared — even though the second instance was running fine, with its own
session and its own window.

The reason: the application runs inside an MSIX container, so its writes to `%APPDATA%`
are redirected into the package's LocalCache. The profile directory actually lands at:

```
%LOCALAPPDATA%\Packages\<PackageFamilyName>\LocalCache\Roaming\<profile>
```

A launcher started from a desktop shortcut runs **outside** the container and sees the
real path — which is never created. The failure condition was therefore satisfied on
every successful launch.

**Verification:** the `Claude-konto2` directory was found under the virtualized path,
with a modification time matching precisely the launch that had been reported as failed.

**Takeaway:** paths handed to an MSIX package on the command line are not the paths where
its data ends up. If you verify a launch by checking for a file or directory, check both
locations.

---

## 2. Your diagnostic tools may be inside the container too

A direct consequence of the previous point, but a separate source of mistakes. A process
started from inside an MSIX container — a terminal opened by the app itself, for
instance — sees a **virtualized** `%APPDATA%`, `%LOCALAPPDATA%`, and
`HKCU\Software\Classes`. Tests run from such a process can report the opposite of
reality. Here they first falsely confirmed that everything worked (because the child
process inherited package identity), and later that the profile directory did not exist
when it did.

Worse: copying a build into `%LOCALAPPDATA%` from such a process lands it in the
container's directory and **does not update the executable the desktop shortcut runs** —
so you can spend a long time debugging a stale binary. That happened here; the installed
file was five minutes older than the build being tested, with no way to tell from the
outside.

**Workaround:** create the process outside the current process tree, via WMI:

```powershell
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
  CommandLine = 'cmd.exe /c <command> > "<output-file>" 2>&1'
}
```

A plain `Start-Process` is not enough — it inherits the container from its parent.

**Takeaway:** when working with MSIX packages, establish first whether your diagnostic
tooling runs inside the container. Otherwise you are debugging a virtualized reality.

---

## 3. `ProcThreadAttributePackageFullName` is 8

`PROC_THREAD_ATTRIBUTE_PACKAGE_FULL_NAME` gives a child process the identity of a
specified MSIX package. It is not in the publicly visible part of the Windows SDK
headers — it sits behind `_USE_FULL_PROC_THREAD_ATTRIBUTE` — so the number has to be
supplied by hand. Microsoft's documentation and search results describe the attribute
but **do not give its numeric value**.

The first attempt used `23`. `UpdateProcThreadAttribute` then returned error **24**
(`ERROR_BAD_LENGTH`) — it was hitting a real but entirely different attribute, one
expecting a fixed-size field rather than a string.

The correct value is **8**. The public `PROC_THREAD_ATTRIBUTE_NUM` enum lists
0, 2, 3, 4, 5, 6, 7, 9, 11, 13 — and the gap at 8 is this attribute.

**Verification is indirect but unambiguous:** after the change to 8, `ERROR_BAD_LENGTH`
disappeared — the attribute is accepted together with the string.

---

## 4. Granting package identity requires already having it

With the attribute value corrected, `CreateProcess` still fails — but now with a
different error: **575** (`ERROR_APP_INIT_FAILURE`).

So knowing the right attribute number is not sufficient. Giving a child process package
identity requires the calling process to have it already — and an ordinary program
started from a desktop shortcut does not.

**Practical consequence:** for a launcher that lives outside the package, this route is a
dead end. It is kept here as the first attempt, since it is cheap and harmless, but in
practice the fallback always does the work.

---

## 5. `ActivateApplication` will not pass arguments to a Win32 app

`IApplicationActivationManager::ActivateApplication` takes an `arguments` parameter,
which looks like the obvious way to pass `--user-data-dir` while preserving MSIX
identity. **It does not work for packaged desktop applications.**

The documentation describes the method as activation for the WinRT **`Windows.Launch`**
contract — `arguments` is delivered to `ILaunchActivatedEventArgs.Arguments`, an API read
by UWP apps. Electron, like any ordinary Win32 application (even packaged as MSIX), does
not implement that contract: it reads arguments from the normal command line, which
`ActivateApplication` does not set.

**The symptom is misleading.** The call returns `S_OK`, so it looks like success. In
reality the app starts *without* the switch, i.e. on the default profile; and when that
profile is already running, Electron forwards the request to the existing instance and
the process exits immediately with code 0. From the outside this looks like the second
instance failing, when it is actually a silent fallback to the first one.

**Takeaway:** `ActivateApplication` is fine for launching a package with no arguments.
To pass command-line switches, use `CreateProcess`.

---

## 6. Immediate process exit is not a failure

`Claude.exe` from the `WindowsApps` directory, started with a plain `CreateProcess`,
exits **immediately with code 0**, having handed the launch off to a process inside the
container. The application starts normally and keeps running.

An earlier assumption in this project was the opposite: that an immediate exit proved the
process had started without package identity and ignored `--user-data-dir`. That
assumption was wrong, and it led to a verification step that produced false alarms.

**Takeaway:** the parent process's exit code says nothing about whether an MSIX-packaged
application launched successfully. Contrary to the earlier suspicion, a plain
`CreateProcess` **does** work — including honouring `--user-data-dir`.

---

## What could not be determined

- Whether `PROC_THREAD_ATTRIBUTE_PACKAGE_FULL_NAME` would work if the launcher itself
  were packaged as MSIX. Not tested — there was no need, since a plain `CreateProcess`
  is sufficient.
- Whether the value 8 is correct across all Windows versions. Confirmed only indirectly
  (by the change in error code) and only on one Windows 11 machine.
