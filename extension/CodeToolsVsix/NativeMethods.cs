using System;
using System.Runtime.InteropServices;

namespace CodeToolsVsix
{
    /// <summary>Mirrors cpptools/diagnostic.h's Severity (cpptools::Severity) exactly - this is
    /// the type NativeEditControl_SetLogSink's callback receives, since NativeEditControl.dll's
    /// Log() (and, through it, cpptools's own logging - see CppEditorControl.cpp's
    /// EnsureCpptoolsLogSinkRegistered) is typed in terms of cpptools::Severity directly rather
    /// than a second, separate severity concept.</summary>
    internal enum Severity
    {
        Note = 0,
        Warning = 1,
        Error = 2,
        Fatal = 3,
    }

    /// <summary>Mirrors NativeEditControl.h's LogSinkCallback. message is never assumed
    /// null-terminated (same discipline as everywhere else this DLL crosses a wchar_t* over the
    /// boundary) - messageLength is authoritative, so this stays IntPtr/UIntPtr rather than a
    /// marshaled `string` parameter (which would implicitly assume null-termination).</summary>
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void LogSinkCallback(Severity severity, IntPtr message, UIntPtr messageLength);

    /// <summary>Mirrors NativeEditControl.h's EditorCommand. Values are explicit on both sides
    /// (not left to implicit ordering) so a mismatch can't silently slip in from reordering either
    /// enum independently.</summary>
    internal enum EditorCommand : uint
    {
        Undo = 0,
        Redo = 1,
        Cut = 2,
        Copy = 3,
        Paste = 4,
        Find = 5,
        Replace = 6,
        GotoLine = 7,
    }

    /// <summary>Mirrors NativeEditControl.h's EditorCommandArgs field-for-field (same order,
    /// same 8-byte-wide fields on x64, so the layouts line up with no padding surprises). The
    /// closest a P/Invoke-crossable C ABI gets to something like std::any - a fixed, generic
    /// payload rather than a true variant, since the managed side has to know the exact shape at
    /// compile time either way. text1/text2 (when non-zero) are never assumed null-terminated on
    /// the native side - the *Length fields are authoritative, same contract as
    /// NativeEditControl_Load/Save's filePath.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct EditorCommandArgs
    {
        public IntPtr Text1;
        public UIntPtr Text1Length;
        public IntPtr Text2;
        public UIntPtr Text2Length;
        public long Number;
    }

    /// <summary>Raw Win32/native P/Invoke declarations used to create and manage the hosted
    /// child HWND. This is the entire native surface: create the window, tell it a path to load
    /// from or save to, ask if it's dirty, dispatch an editing command, destroy it. No document
    /// content (text, symbols, or otherwise) crosses this boundary - NativeEditControl.dll owns
    /// its own file I/O and its own call into cpptools::Parser entirely on the native side (see
    /// CppEditorControl.cpp).</summary>
    internal static class NativeMethods
    {
        /// <summary>CppEditorControl::Create (via EditThreadHost - see NativeEditControl.h's own
        /// comment), exported by the sibling NativeEditControl native project
        /// (NativeEditControl.dll, deployed alongside this assembly - see the csproj's Target for
        /// NativeEditControl.dll). The returned HWND lives on NativeEditControl.dll's own
        /// dedicated background thread, not this thread - close it via
        /// <see cref="NativeEditControl_RequestClose"/>, never <see cref="DestroyWindow"/>
        /// directly. Returns the new control's HWND, or IntPtr.Zero on failure.</summary>
        [DllImport("NativeEditControl.dll", EntryPoint = "NativeEditControl_Create", CallingConvention = CallingConvention.StdCall, SetLastError = true)]
        public static extern IntPtr NativeEditControl_Create(IntPtr hwndParent, int x, int y, int width, int height, IntPtr hInstance);

        /// <summary>Destroys the control behind hwnd - must be used instead of
        /// <see cref="DestroyWindow"/> (see <see cref="NativeEditControl_Create"/>'s own comment
        /// on why: the window lives on a different thread than this one, and DestroyWindow()
        /// must be called from the thread that created the window).</summary>
        [DllImport("NativeEditControl.dll", EntryPoint = "NativeEditControl_RequestClose", CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeEditControl_RequestClose(IntPtr hwnd);

        /// <summary>Reads filePath (native file I/O) and populates the control - including, for
        /// C/C++ source, a cpptools-derived outline - entirely in native code. filePathLength is
        /// explicit (native never assumes filePath is null-terminated, even though the P/Invoke
        /// marshaler does null-terminate a `string` parameter) - pass filePath.Length.</summary>
        [DllImport("NativeEditControl.dll", EntryPoint = "NativeEditControl_Load", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeEditControl_Load(IntPtr hwnd, string filePath, UIntPtr filePathLength);

        /// <summary>Writes the control's current text to filePath (native file I/O). Same
        /// filePathLength contract as <see cref="NativeEditControl_Load"/>.</summary>
        [DllImport("NativeEditControl.dll", EntryPoint = "NativeEditControl_Save", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeEditControl_Save(IntPtr hwnd, string filePath, UIntPtr filePathLength);

        [DllImport("NativeEditControl.dll", EntryPoint = "NativeEditControl_IsDirty", CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeEditControl_IsDirty(IntPtr hwnd);

        /// <summary>Generic dispatch for editor-level editing commands (Undo, Redo, Cut, Copy,
        /// Paste, Find, Replace, GotoLine) - see NativeEditControl.h's EditorCommand/
        /// EditorCommandArgs. flags is a reserved bitmask (currently unused by any command).
        /// Passed by ref rather than as an IntPtr the caller marshals manually - the CLR pins a
        /// ref struct argument and passes its address, matching the native side's
        /// `const EditorCommandArgs*` exactly.</summary>
        [DllImport("NativeEditControl.dll", EntryPoint = "NativeEditControl_ExecCommand", CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeEditControl_ExecCommand(IntPtr hwnd, EditorCommand command, uint flags, ref EditorCommandArgs args);

        /// <summary>Registers a sink NativeEditControl.dll relays its own log lines to (see
        /// Logging.h), in addition to its always-on OutputDebugStringA baseline. sink must be
        /// kept alive by the caller for as long as it stays registered - see
        /// OutputWindowLogger.SinkDelegate's own comment on why.</summary>
        [DllImport("NativeEditControl.dll", EntryPoint = "NativeEditControl_SetLogSink", CallingConvention = CallingConvention.StdCall)]
        public static extern void NativeEditControl_SetLogSink(LogSinkCallback sink);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool DestroyWindow(IntPtr hWnd);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int nWidth, int nHeight, [MarshalAs(UnmanagedType.Bool)] bool bRepaint);

        [DllImport("user32.dll")]
        public static extern IntPtr SetFocus(IntPtr hWnd);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr GetModuleHandle(string lpModuleName);
    }
}
