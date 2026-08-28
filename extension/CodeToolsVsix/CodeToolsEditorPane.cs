using System;
using System.Collections.Generic;
using System.IO;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.OLE.Interop;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace CodeToolsVsix
{
    /// <summary>
    /// The document window for a file opened with this editor, and the object that owns its
    /// persistence. VS talks to this through two roles at once - <see cref="IVsWindowPane"/>
    /// (the on-screen window: give me an HWND, resize it, close it) and
    /// <see cref="IVsPersistDocData"/> (load/save/dirty state for the file). Combining both on
    /// one object is the standard shape for a custom editor that isn't wrapping the normal text
    /// buffer.
    ///
    /// The actual editing surface is a plain native Win32 HWND created and owned by
    /// <see cref="NativeEditHost"/>, parented directly into the frame VS gives us in
    /// <see cref="CreatePaneWindow"/>. This class never touches file content itself - Load/Save
    /// only ever hand a path across to native code and get a pass/fail bool back
    /// (<see cref="NativeEditHost.Load"/>/<see cref="NativeEditHost.Save"/>). The one exception is
    /// <see cref="_isReadOnly"/>, which is file *metadata* (the read-only attribute), not content -
    /// checking it is legitimately this class's job since "should VS allow a save" is a
    /// shell-facing concern, not a document-model one.
    ///
    /// <see cref="IOleCommandTarget"/> currently recognizes ten editing commands (Undo, Redo, Cut,
    /// Copy, Paste, Find, Replace, GotoLine - see <see cref="StubCommands"/>). QueryStatus reports
    /// them enabled so they're reachable; Exec dispatches all of them through one generic native
    /// call (<see cref="NativeEditHost.ExecCommand"/>, mirroring NativeEditControl.h's
    /// EditorCommand/EditorCommandArgs) rather than a P/Invoke per command - the stub behavior
    /// itself (currently just an OutputDebugStringW log) lives natively in
    /// StandInEditControl::ExecCommand, not in this class, same lifecycle/dispatch-only principle
    /// as Load/Save/IsDirty. That's deliberate scoping, not an oversight - the standard command
    /// sets these are drawn from (VSStd97CmdID/VSStd2KCmdID) have 986 and 1131 members respectively
    /// (2117 combined, counted directly off the restored VS SDK assembly), and the overwhelming
    /// majority (build, debug, source control, window management, ...) will never reach a
    /// text-editing surface like this one. Undo/Redo in particular is a real, tracked requirement
    /// once this control does real editing (see the comment on <see cref="StubCommands"/>) - not
    /// meant to stay a stub - unlike Find/Replace, which a real implementation would likely want to
    /// hand off to VS's own Find UI via <c>IVsFindTarget</c> rather than reimplement locally.
    ///
    /// <see cref="IVsFileChangeEvents"/> is also implemented as a stub: this pane subscribes to
    /// external changes on its file via SVsFileChangeEx (<see cref="RefreshFileChangeAdvise"/>),
    /// but <see cref="FilesChanged"/> only logs - a real implementation would reload or prompt
    /// depending on dirty state. The subscription itself is real, not a stub - including wrapping
    /// our own writes in IgnoreFile so Save doesn't trigger a spurious self-notification.
    ///
    /// Swap <see cref="NativeEditHost.CreateChildWindow"/> for your real control and the rest of
    /// this class (load/save/dirty/resize plumbing) keeps working unchanged.
    /// </summary>
    internal sealed class CodeToolsEditorPane :
        IVsWindowPane,
        IOleCommandTarget,
        IVsPersistDocData,
        IVsFileChangeEvents,
        IDisposable
    {
        /// <summary>
        /// (command-set GUID, command ID) -> the <see cref="EditorCommand"/> this pane recognizes
        /// as a stub, dispatched generically through <see cref="NativeEditHost.ExecCommand"/> (see
        /// NativeEditControl.h's EditorCommand/EditorCommandArgs for the native side). Several
        /// logical commands (Undo, Redo, Cut, Copy, Paste, Find, Replace) exist in *both* the
        /// legacy VSStd97 set and the newer VSStd2K set under different numeric IDs - VS can route
        /// a given keystroke/menu click through either depending on context, so both are listed.
        /// GotoLine only exists under VSStd2K.
        ///
        /// TODO: once real editing exists, Undo/Redo need a real decision - integrate with VS's
        /// shared IOleUndoManager (participates in VS's own Undo stack/toolbar) vs. a private undo
        /// stack owned entirely by the native control (simpler, but invisible to VS's Undo menu).
        /// Not decided yet; this table exists so that decision can be made once there's something
        /// to undo, not guessed at now.
        /// </summary>
        private static readonly Dictionary<(Guid Group, uint Id), EditorCommand> StubCommands =
            new Dictionary<(Guid, uint), EditorCommand>
            {
                [(VSConstants.GUID_VSStandardCommandSet97, (uint)VSConstants.VSStd97CmdID.Undo)] = EditorCommand.Undo,
                [(VSConstants.GUID_VSStandardCommandSet97, (uint)VSConstants.VSStd97CmdID.MultiLevelUndo)] = EditorCommand.Undo,
                [(VSConstants.VSStd2K, (uint)VSConstants.VSStd2KCmdID.UNDO)] = EditorCommand.Undo,

                [(VSConstants.GUID_VSStandardCommandSet97, (uint)VSConstants.VSStd97CmdID.Redo)] = EditorCommand.Redo,
                [(VSConstants.GUID_VSStandardCommandSet97, (uint)VSConstants.VSStd97CmdID.MultiLevelRedo)] = EditorCommand.Redo,
                [(VSConstants.VSStd2K, (uint)VSConstants.VSStd2KCmdID.REDO)] = EditorCommand.Redo,

                [(VSConstants.GUID_VSStandardCommandSet97, (uint)VSConstants.VSStd97CmdID.Cut)] = EditorCommand.Cut,
                [(VSConstants.VSStd2K, (uint)VSConstants.VSStd2KCmdID.CUT)] = EditorCommand.Cut,

                [(VSConstants.GUID_VSStandardCommandSet97, (uint)VSConstants.VSStd97CmdID.Copy)] = EditorCommand.Copy,
                [(VSConstants.VSStd2K, (uint)VSConstants.VSStd2KCmdID.COPY)] = EditorCommand.Copy,

                [(VSConstants.GUID_VSStandardCommandSet97, (uint)VSConstants.VSStd97CmdID.Paste)] = EditorCommand.Paste,
                [(VSConstants.VSStd2K, (uint)VSConstants.VSStd2KCmdID.PASTE)] = EditorCommand.Paste,

                [(VSConstants.GUID_VSStandardCommandSet97, (uint)VSConstants.VSStd97CmdID.Find)] = EditorCommand.Find,
                [(VSConstants.VSStd2K, (uint)VSConstants.VSStd2KCmdID.FIND)] = EditorCommand.Find,

                [(VSConstants.GUID_VSStandardCommandSet97, (uint)VSConstants.VSStd97CmdID.Replace)] = EditorCommand.Replace,
                [(VSConstants.VSStd2K, (uint)VSConstants.VSStd2KCmdID.REPLACE)] = EditorCommand.Replace,

                [(VSConstants.VSStd2K, (uint)VSConstants.VSStd2KCmdID.GOTOLINE)] = EditorCommand.GotoLine,
            };

        private readonly NativeEditHost _host = new NativeEditHost();

        private Microsoft.VisualStudio.OLE.Interop.IServiceProvider _site;
        private string _filePath;
        private string _pendingLoadPath;

        /// <summary>Set from the file's read-only attribute at Load time (metadata, not content -
        /// see class remarks). Gates <see cref="SaveDocData"/> only, for now: there's no real
        /// editing yet for a read-only file to meaningfully block at the control level, but Save
        /// silently overwriting a read-only file is a real bug worth closing now. A full
        /// implementation would also surface VS's usual "file is read-only" affordance (visually
        /// disabling edits, prompting to check out/make writable) - not done here.</summary>
        private bool _isReadOnly;

        /// <summary>SVsFileChangeEx, fetched lazily on first use. Watches <see cref="_filePath"/>
        /// for external changes (another tool/process writing to the file outside this editor);
        /// see <see cref="RefreshFileChangeAdvise"/>/<see cref="FilesChanged"/>.</summary>
        private IVsFileChangeEx _fileChangeService;

        /// <summary>Non-zero while <see cref="_fileChangeService"/> is watching <see cref="_filePath"/>
        /// on our behalf; the handle AdviseFileChange/UnadviseFileChange use.</summary>
        private uint _fileChangeCookie;

        public CodeToolsEditorPane(string mkDocument)
        {
            _filePath = mkDocument;
        }

        // ----------------------------------------------------------------
        // IVsWindowPane - the visible window
        // ----------------------------------------------------------------

        int IVsWindowPane.SetSite(Microsoft.VisualStudio.OLE.Interop.IServiceProvider psp)
        {
            _site = psp;
            return VSConstants.S_OK;
        }

        int IVsWindowPane.CreatePaneWindow(IntPtr hwndParent, int x, int y, int cx, int cy, out IntPtr hwnd)
        {
            hwnd = _host.CreateChildWindow(hwndParent, x, y, cx, cy);

            // LoadDocData can run before or after the window exists depending on how the doc
            // was opened; flush whichever arrived first now that both are ready.
            if (_pendingLoadPath != null)
            {
                _host.Load(_pendingLoadPath);
                _pendingLoadPath = null;
            }

            return VSConstants.S_OK;
        }

        int IVsWindowPane.GetDefaultSize(SIZE[] pSize)
        {
            if (pSize != null && pSize.Length > 0)
            {
                pSize[0].cx = 400;
                pSize[0].cy = 300;
            }

            return VSConstants.S_OK;
        }

        int IVsWindowPane.ClosePane()
        {
            UnadviseFileChangeCore();
            _host.Destroy();
            return VSConstants.S_OK;
        }

        int IVsWindowPane.LoadViewState(IStream pStream) => VSConstants.S_OK;

        int IVsWindowPane.SaveViewState(IStream pStream) => VSConstants.S_OK;

        int IVsWindowPane.TranslateAccelerator(MSG[] lpmsg)
        {
            // Not handled here - let the message flow through the normal Win32 dispatch to our
            // native child (and, failing that, VS's own accelerator table).
            return VSConstants.E_FAIL;
        }

        // ----------------------------------------------------------------
        // IOleCommandTarget - stubs for the editing commands in StubCommands (see class remarks
        // for why only these ten, out of 2117 possible VSStd97/VSStd2K command IDs); everything
        // else is declined so the shell/global handlers get it, same as before.
        // ----------------------------------------------------------------

        int IOleCommandTarget.QueryStatus(ref Guid pguidCmdGroup, uint cCmds, OLECMD[] prgCmds, IntPtr pCmdText)
        {
            if (prgCmds != null && cCmds > 0 && StubCommands.ContainsKey((pguidCmdGroup, prgCmds[0].cmdID)))
            {
                prgCmds[0].cmdf = (uint)(OLECMDF.OLECMDF_SUPPORTED | OLECMDF.OLECMDF_ENABLED);
                return VSConstants.S_OK;
            }

            return (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;
        }

        int IOleCommandTarget.Exec(ref Guid pguidCmdGroup, uint nCmdID, uint nCmdexecopt, IntPtr pvaIn, IntPtr pvaOut)
        {
            if (StubCommands.TryGetValue((pguidCmdGroup, nCmdID), out EditorCommand command))
            {
                // Recognized and claimed (returns S_OK) so it doesn't fall through to some other
                // handler that would also do nothing useful. The actual stub behavior (logging,
                // for now) lives natively - see StandInEditControl::ExecCommand - not here: this
                // class stays lifecycle/dispatch-only, same principle as Load/Save/IsDirty.
                // EditorCommandArgs is left at its default (all zero) - pvaIn isn't decoded yet,
                // so there's no real text1/text2/number to populate. A real Find/Replace/GotoLine
                // implementation would decode pvaIn's VARIANT here to fill those in.
                _host.ExecCommand(command);
                return VSConstants.S_OK;
            }

            return (int)Microsoft.VisualStudio.OLE.Interop.Constants.OLECMDERR_E_NOTSUPPORTED;
        }

        // ----------------------------------------------------------------
        // IVsPersistDocData - load/save/dirty tracking for the file on disk. Dirty state is
        // native's own (NativeEditHost.IsDirty), not tracked here - this class holds no document
        // content state of its own at all.
        // ----------------------------------------------------------------

        int IVsPersistDocData.GetGuidEditorType(out Guid pClassID)
        {
            pClassID = EditorGuids.Factory;
            return VSConstants.S_OK;
        }

        int IVsPersistDocData.IsDocDataDirty(out int pfDirty)
        {
            pfDirty = _host.IsDirty() ? 1 : 0;
            return VSConstants.S_OK;
        }

        int IVsPersistDocData.SetUntitledDocPath(string pszDocDataPath)
        {
            _filePath = pszDocDataPath;
            return VSConstants.S_OK;
        }

        int IVsPersistDocData.LoadDocData(string pszMkDocument) => LoadDocDataCore(pszMkDocument);

        private int LoadDocDataCore(string pszMkDocument)
        {
            _filePath = pszMkDocument;
            _isReadOnly = IsFileReadOnly(pszMkDocument);

            if (_host.Handle != IntPtr.Zero)
            {
                _host.Load(_filePath);
            }
            else
            {
                // Window not created yet - CreatePaneWindow will flush this once it exists.
                _pendingLoadPath = _filePath;
            }

            RefreshFileChangeAdvise();

            // A false result here (e.g. a brand-new/untitled path that doesn't exist on disk yet)
            // isn't treated as a hard failure - same leniency the file-content version of this
            // code had for a missing file, just expressed as "native starts from nothing" instead
            // of "content is the empty string".
            return VSConstants.S_OK;
        }

        // ----------------------------------------------------------------
        // IVsFileChangeEx / IVsFileChangeEvents - watches the file on disk for changes made
        // outside this editor (another tool, git checkout, ...). RefreshFileChangeAdvise/
        // UnadviseFileChangeCore manage the subscription; FilesChanged is the actual
        // notification callback VS invokes when the watched file changes.
        // ----------------------------------------------------------------

        private IVsFileChangeEx GetFileChangeService()
        {
            return _fileChangeService ?? (_fileChangeService = Package.GetGlobalService(typeof(SVsFileChangeEx)) as IVsFileChangeEx);
        }

        /// <summary>(Re-)subscribes to external changes on <see cref="_filePath"/>, first
        /// unsubscribing any previous watch (so this is safe to call repeatedly - e.g. every
        /// Load/Reload - without leaking cookies). No-op if the file doesn't exist yet (a
        /// brand-new/untitled doc has nothing to watch) or the service isn't available.</summary>
        private void RefreshFileChangeAdvise()
        {
            UnadviseFileChangeCore();

            if (_filePath == null || !File.Exists(_filePath))
            {
                return;
            }

            IVsFileChangeEx service = GetFileChangeService();
            if (service == null)
            {
                return;
            }

            const uint watchFlags = (uint)(_VSFILECHANGEFLAGS.VSFILECHG_Time
                                            | _VSFILECHANGEFLAGS.VSFILECHG_Size
                                            | _VSFILECHANGEFLAGS.VSFILECHG_Del);

            // HRESULT >= 0 is success by convention - avoids pulling in ErrorHandler just for this.
            if (service.AdviseFileChange(_filePath, watchFlags, this, out uint cookie) >= 0)
            {
                _fileChangeCookie = cookie;
            }
        }

        private void UnadviseFileChangeCore()
        {
            if (_fileChangeCookie != 0 && _fileChangeService != null)
            {
                _fileChangeService.UnadviseFileChange(_fileChangeCookie);
            }

            _fileChangeCookie = 0;
        }

        int IVsFileChangeEvents.FilesChanged(uint cChanges, string[] rgpszFile, uint[] rggrfChange)
        {
            // STUB: a full implementation would check IsDocDataDirty and either silently reload
            // (native Load again) when clean, or surface VS's usual "file changed on disk - reload
            // and lose your changes?" prompt when dirty. For now this only proves the subscription
            // is alive; SaveDocData's own IgnoreFile wrapping (below) keeps this from firing for
            // our own writes.
            System.Diagnostics.Debug.WriteLine(
                $"CodeToolsEditorPane: FilesChanged stub - '{_filePath}' changed on disk externally (not yet implemented: no reload/prompt).");
            return VSConstants.S_OK;
        }

        int IVsFileChangeEvents.DirectoryChanged(string pszDirectory) => VSConstants.S_OK;

        /// <summary>Checks the file's read-only *attribute* only - metadata, not content, so this
        /// doesn't conflict with keeping document content entirely on the native side.</summary>
        private static bool IsFileReadOnly(string path)
        {
            try
            {
                return path != null && File.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReadOnly) != 0;
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                return false;
            }
        }

        int IVsPersistDocData.SaveDocData(VSSAVEFLAGS dwSave, out string pbstrMkDocumentNew, out int pfSaveCanceled)
        {
            pbstrMkDocumentNew = null;

            if (_isReadOnly)
            {
                // STUB: a full implementation would surface VS's usual "file is read-only, check
                // out / make writable?" prompt here instead of just refusing silently-ish (a
                // debug-only log).
                System.Diagnostics.Debug.WriteLine($"CodeToolsEditorPane: SaveDocData refused - '{_filePath}' is read-only.");
                pfSaveCanceled = 1;
                return VSConstants.E_FAIL;
            }

            if (dwSave == VSSAVEFLAGS.VSSAVE_SaveAs || dwSave == VSSAVEFLAGS.VSSAVE_SaveCopyAs)
            {
                // STUB: Save As / Save Copy As should prompt for a new path (and, for SaveAs only,
                // return it via pbstrMkDocumentNew so VS updates the tab/moniker) - not implemented
                // yet, so this still just saves back to the original path.
                System.Diagnostics.Debug.WriteLine($"CodeToolsEditorPane: SaveDocData got {dwSave} - not yet implemented, saving to original path instead.");
            }

            // Suppress the file-change notification our own write is about to cause - otherwise
            // FilesChanged (above) would immediately fire for a change we already know about.
            if (_fileChangeCookie != 0 && _fileChangeService != null)
            {
                _fileChangeService.IgnoreFile(_fileChangeCookie, _filePath, 1);
            }

            pfSaveCanceled = _host.Save(_filePath) ? 0 : 1;

            if (_fileChangeCookie != 0 && _fileChangeService != null)
            {
                _fileChangeService.IgnoreFile(_fileChangeCookie, _filePath, 0);
            }

            return pfSaveCanceled == 0 ? VSConstants.S_OK : VSConstants.E_FAIL;
        }

        int IVsPersistDocData.Close()
        {
            UnadviseFileChangeCore();
            _host.Destroy();
            return VSConstants.S_OK;
        }

        int IVsPersistDocData.OnRegisterDocData(uint docCookie, IVsHierarchy pHierNew, uint itemidNew) => VSConstants.S_OK;

        int IVsPersistDocData.RenameDocData(uint grfAttribs, IVsHierarchy pHierNew, uint itemidNew, string pszMkDocumentNew)
        {
            _filePath = pszMkDocumentNew;
            RefreshFileChangeAdvise();
            return VSConstants.S_OK;
        }

        int IVsPersistDocData.IsDocDataReloadable(out int pfReloadable)
        {
            pfReloadable = 1;
            return VSConstants.S_OK;
        }

        int IVsPersistDocData.ReloadDocData(uint grfFlags) => LoadDocDataCore(_filePath);

        public void Dispose()
        {
            UnadviseFileChangeCore();
            _host.Dispose();
        }
    }
}
