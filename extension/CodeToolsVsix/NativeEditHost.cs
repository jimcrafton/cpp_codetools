using System;

namespace CodeToolsVsix
{
    /// <summary>
    /// Owns the native Win32 child HWND hosted inside the VS document tab. This is the single
    /// integration point for wiring in a real editing control: everything above this class
    /// (<see cref="CodeToolsEditorPane"/>, the editor factory, the package registration) only
    /// ever talks to it through <see cref="Handle"/>, <see cref="CreateChildWindow"/>,
    /// <see cref="Resize"/>, <see cref="Load"/>, <see cref="Save"/>, and <see cref="IsDirty"/> -
    /// none of it cares what kind of window is actually behind that handle, or what the document
    /// model behind it looks like.
    ///
    /// Deliberately kept to a lifecycle-only surface: this class (and the managed layer above it)
    /// never sees document content - no text, no symbols, nothing richer. NativeEditControl.dll
    /// owns its own file I/O and, for C/C++ source, its own call into cpptools::Parser, entirely
    /// on the native side (see CppEditorControl.cpp). That's deliberate: it's what let the
    /// native document model become a real newui::RootView/TextControl (see
    /// EditThreadHost.cpp/CppEditorControl.cpp) behind the same four calls, with this class - and
    /// everything above it - never needing to represent that model in managed code.
    /// </summary>
    internal sealed class NativeEditHost : IDisposable
    {
        private IntPtr _hwnd;

        public IntPtr Handle => _hwnd;

        /// <summary>
        /// Creates the child window that becomes the document's editing surface.
        /// <paramref name="hwndParent"/> is the HWND VS gives the pane via
        /// <c>IVsWindowPane.CreatePaneWindow</c> (see <c>CodeToolsEditorPane.CreatePaneWindow</c>);
        /// the returned HWND is handed straight back to VS. VS's frame repositions/resizes that
        /// HWND directly (via SetWindowPos on the handle) whenever the document tab resizes, the
        /// same way it always has for native HWND-based panes - no callback into managed code is
        /// needed for that. <see cref="Resize"/> below is exposed only in case your own control
        /// needs to explicitly reposition itself (e.g. from other native code).
        /// </summary>
        public IntPtr CreateChildWindow(IntPtr hwndParent, int x, int y, int width, int height)
        {
            // >>> INTEGRATION POINT <<<
            // To host a different native control, replace this call (and NativeEditControl.dll
            // it P/Invokes into) with your own. Keep it WS_CHILD, parented to hwndParent, and
            // sized to (x,y,width,height).
            IntPtr hInstance = NativeMethods.GetModuleHandle(null);

            _hwnd = NativeMethods.NativeEditControl_Create(hwndParent, x, y, width, height, hInstance);

            if (_hwnd == IntPtr.Zero)
            {
                int error = System.Runtime.InteropServices.Marshal.GetLastWin32Error();
                OutputWindowLogger.LogFromManaged($"codetools++ NativeEditControl_Create FAILED (hwndParent=0x{hwndParent.ToInt64():X}, size={width}x{height}, Win32 error {error})");
                throw new InvalidOperationException($"NativeEditControl_Create failed with Win32 error {error}.");
            }

            // Confirms codetools++ specifically is what handled this file (not a same-purposed,
            // unrelated extension registered for the same file types - confirmed live, 2026-08-29,
            // that this can happen silently otherwise). Logged from here (managed code, this
            // call's own thread), not from native code on EditThreadHost's dedicated thread - see
            // OutputWindowLogger.Initialize()'s own comment on why that's the unsafe direction.
            OutputWindowLogger.LogFromManaged($"codetools++ NativeEditControl_Create: hwnd=0x{_hwnd.ToInt64():X} size={width}x{height}");

            return _hwnd;
        }

        /// <summary>Called whenever VS resizes the document tab; keeps the child filling it.</summary>
        public void Resize(int x, int y, int width, int height)
        {
            if (_hwnd != IntPtr.Zero)
            {
                NativeMethods.MoveWindow(_hwnd, x, y, width, height, true);
            }
        }

        public void SetFocus()
        {
            if (_hwnd != IntPtr.Zero)
            {
                NativeMethods.SetFocus(_hwnd);
            }
        }

        /// <summary>Tells the control to load filePath - native code does its own file read and,
        /// for C/C++ source, its own cpptools parse. Returns false if there's no window yet or
        /// the native load failed (missing/unreadable file).</summary>
        public bool Load(string filePath)
        {
            if (_hwnd == IntPtr.Zero || filePath == null)
            {
                return false;
            }

            return NativeMethods.NativeEditControl_Load(_hwnd, filePath, (UIntPtr)filePath.Length);
        }

        /// <summary>Tells the control to save its current content to filePath - native code does
        /// its own file write. Returns false if there's no window yet or the native save
        /// failed.</summary>
        public bool Save(string filePath)
        {
            if (_hwnd == IntPtr.Zero || filePath == null)
            {
                return false;
            }

            return NativeMethods.NativeEditControl_Save(_hwnd, filePath, (UIntPtr)filePath.Length);
        }

        public bool IsDirty()
        {
            return _hwnd != IntPtr.Zero && NativeMethods.NativeEditControl_IsDirty(_hwnd);
        }

        /// <summary>Dispatches an editor-level command (Undo, Redo, Cut, Copy, Paste, Find,
        /// Replace, GotoLine) to native code - see NativeMethods.EditorCommand/
        /// EditorCommandArgs and NativeEditControl.h. args defaults to empty (every field zero);
        /// callers that need to populate text1/text2/number pass their own.</summary>
        public bool ExecCommand(EditorCommand command, uint flags = 0, EditorCommandArgs args = default)
        {
            if (_hwnd == IntPtr.Zero)
            {
                return false;
            }

            return NativeMethods.NativeEditControl_ExecCommand(_hwnd, command, flags, ref args);
        }

        public void Destroy()
        {
            if (_hwnd != IntPtr.Zero)
            {
                // NativeEditControl_RequestClose, not DestroyWindow directly - the control's
                // real HWND lives on NativeEditControl.dll's own dedicated background thread
                // (see NativeMethods.NativeEditControl_Create's own comment), and DestroyWindow()
                // must be called from the thread that created the window. This blocks until the
                // native side has really torn it down on that thread, same as a direct
                // DestroyWindow() call would have guaranteed.
                NativeMethods.NativeEditControl_RequestClose(_hwnd);
                _hwnd = IntPtr.Zero;
            }
        }

        public void Dispose() => Destroy();
    }
}
