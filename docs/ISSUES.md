# Issues

## Resolved bugs

### CLI intermittently deadlocks at process exit (Windows loader shutdown)

The CLI occasionally wedges and never exits (~0.2 % of invocations). Under `ctest`
this surfaced as `cli-tests` tripping its `TIMEOUT 120`; a normal run is ~10 s.

**Root cause (2026-07-17, confirmed by a gdb stack of two separately-caught wedges,
one fresh, one a 99-min-old instance — identical stacks): a deadlock in the Windows
loader's process-teardown, not our logic, not Python, not AV.** The main thread, after
`main()` returns:

```
__tmainCRTStartup → _initterm_e (static dtors) → _exit → RtlExitUserProcess
  → ntdll!LdrShutdownProcess          # loader runs DLL_PROCESS_DETACH for ~130 DLLs
    → RtlRunOnceExecuteOnce
      → ntdll!ZwWaitForAlertByThreadId # blocks on a RunOnce/loader lock, never returns
```

Only two threads exist at that point: the main thread (stuck above) and gdb's break-in
thread — every libvips/glib worker has already exited. So the hang is purely in unloading
the enormous libvips dependency graph (rsvg→cairo→pango→harfbuzz, curl→aws-c-*→openssl,
glib/gio, …); one DLL's detach handler waits on a RunOnce that never completes. A known,
rare Windows failure mode when many DLLs with background-thread machinery tear down.

Why earlier theories were wrong (recorded so we don't relitigate):
- **Not Python.** Python was only blocked in `subprocess` waiting for the child to exit.
- **Not our logic / not `vips_shutdown`.** `main()` had already returned; the wedge is
  below it in CRT/loader teardown.
- **Not AV/EDR.** An earlier guess based on un-reapable zombie records; the stack shows an
  in-process loader-lock wait, not an external syscall. (The zombies are the same wedge —
  a process stuck in `LdrShutdownProcess` never finishes exiting.)
- **Only reproduces under pytest's rapid back-to-back spawning** (~200 procs/run); ~1050
  slower direct invocations never hit it — timing-sensitive, as a teardown race would be.
- **Not MinGW-specific.** An MSVC build (`msvc-release`, our code via cl.exe) reproduces it
  too: 1 hang / 25 suites, same signature (exit-time wedge caught by the per-call timeout).
  So it is the third-party DLL teardown deadlocking, independent of the compiler that built
  our exe. Note the MSVC build links the reduced `vips-dev-x64-web` set (38 DLLs vs ~130 in
  the MSYS2 build), yet still hangs — a lighter graph lowers the rate but does not remove it,
  which strengthens "the DLL-graph teardown is the culprit". We cannot fix third-party
  `DllMain`; the fix is to not run it (below).

**Fix — APPLIED and verified (`cli/main.cpp`, Windows `main()`).** A CLI has nothing to clean
up that the OS won't reclaim, so running the DLLs' detach handlers is pure risk. After
`runCli()` returns, the Windows `main()` flushes stdout/stderr (`std::cout/cerr.flush()` +
`std::fflush(nullptr)` — mandatory, since many messages end in `'\n'` not `std::endl`) and
exits via `TerminateProcess(GetCurrentProcess(), code)`, bypassing loader teardown entirely.
`_exit`/`ExitProcess` do NOT help — the stack shows `_exit` itself calls `LdrShutdownProcess`.
Scope: Windows only (`#ifdef _WIN32`); Linux returns normally. `vips_shutdown()` still runs
inside `runCli()` before the return, so vips temp cleanup is unaffected.

Verified across both compilers (was ~1/8 MinGW, 1/25 MSVC before):
- **MinGW debug: 0 hangs / 40 suites**, no new zombie processes, 58/58 ctest.
- **MSVC release: 0 hangs / 30 suites** — same fix compiles and works under cl.exe, and the
  hang was compiler-independent so this closes the loop.

Still TODO: **the GUI shares this risk** (same DLL graph, hang on window close) and should get
the same fast-exit on its quit path — but that is Qt-side and needs a Qt Creator build.

Note: rebuilding the MinGW **release** CLI is currently blocked because two pre-fix wedged
`platemaker-cli.exe` zombies hold `build/mingw-release/bin/platemaker-cli.exe` open
(`ld: cannot open output file … Permission denied`). They are stuck in `LdrShutdownProcess`
and cannot be force-killed — a reboot clears them, after which the release build links the
same fixed `main.cpp`. Debug + MSVC already prove the fix.

Test hardening already in place (`tests/cli-tests/conftest.py`), keep regardless of the fix:
- **Per-call CLI timeout** (`_CLI_CALL_TIMEOUT` = 15 s, autouse fixture): a wedge becomes one
  clearly failed test naming the culprit instead of a bare 120 s suite `Timeout`. Validated
  live — caught a fresh wedge as `1 error in 24.68s`.
- **`--pm-trace` / `PM_TRACE`** (default `TRACE_DEFAULT`): timestamped START/END per test.
  Flip `TRACE_DEFAULT` to `False` to quiet it once this is closed out.

Leftover: a couple of wedged `platemaker-cli.exe` records may linger un-reapable until a
reboot (a process stuck in `LdrShutdownProcess` can't be force-killed cleanly).