# VSIX extension

- `codetools++.sln` has ONE project, `CodeToolsVsix` (C# AsyncPackage, registers the `codetools++` editor
  factory, priority 100, below the built-in editor). One config: `Debug|Any CPU`.
- `NativeEditControls.dll` is NOT in the .sln. It's a CMake SHARED target, built by the csproj's
  `BuildAndIncludeNativeDependencies` Target (builds cpptools/newui/blend2d transitively).
- Native C ABI: `NativeEditControl_Create/RequestClose/Load/Save/IsDirty/ExecCommand`, plus
  `NativeEditControl_SetHost(const HostServices*)` (`HostServices.h`, built by `HostConnection.cs`).
  `HostServices` is a versioned, append-only callback table. A new host capability is a new table
  member, not a new export.

## Threading
- Hosts a `newui::RootView` + `TextControl` (`CppEditor.cpp`) on a dedicated background thread with its own
  `RunLoop` (`NativeEditManager::startRunLoop()`), started lazily and shared by all tabs.
- The HWND lives on that thread: close with `NativeEditControl_RequestClose()`, never `DestroyWindow()`.
- Other exports marshal via `NativeEditManager::runOnEditThread()` (`RunLoop::postAndWait()`). The wait
  pumps the UI thread's messages (re-entrant), so anything native needs FROM VS must be an async callback
  (`HostEditorBridge.h`, `HostDocumentEditor.cs`), never a synchronous call into the UI thread.
- `runOnEditThread()` flushes queued log lines (`flushQueuedLogs()`) to the managed sink afterward.

## Build
```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d D:\code\cpp_codetools && msbuild extension\codetools++.sln /p:Configuration=Debug /p:Platform="Any CPU"'
```
There is no `Deploy` target (MSB4057).

## Deploying to the experimental instance
A command-line msbuild does NOT deploy. Only F5 in the IDE does. After a CLI build:
1. Find the hive: `Get-ChildItem "$env:LOCALAPPDATA\Microsoft\VisualStudio" -Filter "18.0_*Exp" | Get-ChildItem -Recurse -Filter CodeToolsVsix.dll`
   (hash-suffixed folder, e.g. `18.0_c2e25beeExp`; newest is usually live, but confirm).
2. Copy `CodeToolsVsix.dll`, `CodeToolsVsix.pkgdef`, `NativeEditControls.dll`, `extension.vsixmanifest`
   into that hive's `Extensions\<8-char>\`. Don't use VSIXInstaller: it no-ops because the version is
   always 1.0.0. If you do, `/rootSuffix` must be the FULL suffix minus `18.0_`, or it creates a junk hive.
3. Relaunch: `Start-Process devenv.exe -ArgumentList "/rootsuffix <full suffix>"`
4. Open files via **File > Open With... > codetools++**. Double-click always uses the default editor.

## vs-debug MCP bridge (`.mcp.json`, `.claude/vs-mcp-shim.ps1`)
Needs "Allow Claude to drive debugger" enabled in the Claude Code panel of that VS window. It only
reaches the main window, never the experimental instance.