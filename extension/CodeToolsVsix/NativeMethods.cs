using System;
using System.Runtime.InteropServices;

namespace CodeToolsVsix
{
    /// <summary>Mirrors cpptools/diagnostic.h's Severity (cpptools::Severity) exactly - this is
    /// the type the HostServices logSink callback receives, since NativeEditControls.dll's
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

    /// <summary>Mirrors NativeEditor.h's LogSinkCallback. message is never assumed
    /// null-terminated (same discipline as everywhere else this DLL crosses a wchar_t* over the
    /// boundary) - messageLength is authoritative, so this stays IntPtr/UIntPtr rather than a
    /// marshaled `string` parameter (which would implicitly assume null-termination).</summary>
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void LogSinkCallback(Severity severity, IntPtr message, UIntPtr messageLength);

    /// <summary>Mirrors HostServices.h's HostServices - everything this host offers the native editors,
    /// handed over in ONE call (NativeEditControl_SetHost). Each member is a function pointer from
    /// Marshal.GetFunctionPointerForDelegate (the delegate must stay alive while registered), or
    /// IntPtr.Zero for a capability this host doesn't provide. Versioned: members are only ever
    /// APPENDED, in the same order as the native struct, and Size is what this build of the struct
    /// measures - native reads only that many bytes, so an older host leaves newer members null.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct HostServices
    {
        public uint Size;
        public IntPtr LogSink;        // LogSinkCallback
        public IntPtr GetText;        // HostGetTextCallback      (with ApplyEdits: files open in VS's editor)
        public IntPtr ApplyEdits;     // HostApplyEditsCallback
        public IntPtr OpenLocation;   // HostOpenLocationCallback
    }

    /// <summary>Mirrors NativeEditControlApi.h's DocumentType - which concrete NativeEditor
    /// subclass NativeEditControl_Create should instantiate. VS itself never tells this extension
    /// what kind of document is being opened (editor-factory selection is a pure
    /// filename-extension match, resolved before CreateEditorInstance is even called - see
    /// CodeToolsEditorFactory.CreateEditorInstance); CodeToolsEditorPane.DocumentTypeFromPath is
    /// where that decision gets made explicit, from the document's own extension, so it can be
    /// carried across the ABI instead of NativeEditManager::createEditor picking one fixed editor
    /// type. Values are explicit on both sides, same discipline as EditorCommand below.</summary>
    internal enum DocumentType : int
    {
        UnrecognizedType = 0,
        CppSource = 1,
        Designer = 2
    }

    /// <summary>Mirrors NativeEditControlApi.h's ToolWindowType - which native tool-window pane
    /// NativeToolWindow_Create builds. A tool window is not a document, so it has its own enum rather than a
    /// <see cref="DocumentType"/>.</summary>
    internal enum ToolWindowType : int
    {
        Unknown = 0,
        ProjectExplorer = 1
    }

    /// <summary>Mirrors NativeEditor.h's EditorCommand. Values are explicit on both sides
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

    /// <summary>Mirrors NativeEditor.h's EditorCommandArgs field-for-field (same order,
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
    /// content (text, symbols, or otherwise) crosses this boundary - NativeEditControls.dll owns
    /// its own file I/O and its own call into cpptools::Parser entirely on the native side (see
    /// CppEditorControl.cpp).</summary>
    internal static class NativeMethods
    {
        /// <summary>CppEditorControl::Create (via NativeEditManager::runOnEditThread() - see NativeEditor.h's own
        /// comment), exported by the sibling NativeEditControl native project
        /// (NativeEditControls.dll, deployed alongside this assembly - see the csproj's Target for
        /// NativeEditControls.dll). The returned HWND lives on NativeEditControls.dll's own
        /// dedicated background thread, not this thread - close it via
        /// <see cref="NativeEditControl_RequestClose"/>, never <see cref="DestroyWindow"/>
        /// directly. Returns the new control's HWND, or IntPtr.Zero on failure.</summary>
        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_Create", CallingConvention = CallingConvention.Cdecl, SetLastError = true)]
        public static extern IntPtr NativeEditControl_Create(IntPtr hwndParent, int x, int y, int width, int height, DocumentType documentType);


        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_SetServiceProvider", CallingConvention = CallingConvention.Cdecl, SetLastError = true)]
        public static extern bool NativeEditControl_SetServiceProvider([MarshalAs(UnmanagedType.IUnknown)] object servicerProviderPtr);


        [DllImport("NativeEditControls.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        public static extern void NativeEditControl_SettingChanged(string settingName, string value);

        /// <summary>What is open: the workspace's root folders (separated by '\n'; empty when nothing is) and
        /// the active build configuration. Pushed by <see cref="WorkspaceTracker"/> as solutions and
        /// folders open and close.</summary>
        [DllImport("NativeEditControls.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        public static extern void NativeEditControl_WorkspaceChanged(string roots, string configuration);

        /// <summary>Creates a tool-window pane (the project explorer) as a child of hwndParent. Like
        /// <see cref="NativeEditControl_Create"/>, the window lives on native's edit thread: close it with
        /// <see cref="NativeToolWindow_RequestClose"/>, never DestroyWindow.</summary>
        [DllImport("NativeEditControls.dll", EntryPoint = "NativeToolWindow_Create", CallingConvention = CallingConvention.Cdecl, SetLastError = true)]
        public static extern IntPtr NativeToolWindow_Create(IntPtr hwndParent, int x, int y, int width, int height, ToolWindowType type);

        [DllImport("NativeEditControls.dll", EntryPoint = "NativeToolWindow_SetBounds", CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeToolWindow_SetBounds(IntPtr hwnd, int x, int y, int width, int height);

        [DllImport("NativeEditControls.dll", EntryPoint = "NativeToolWindow_RequestClose", CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeToolWindow_RequestClose(IntPtr hwnd);


        /// <summary>Destroys the control behind hwnd - must be used instead of
        /// <see cref="DestroyWindow"/> (see <see cref="NativeEditControl_Create"/>'s own comment
        /// on why: the window lives on a different thread than this one, and DestroyWindow()
        /// must be called from the thread that created the window).</summary>
        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_RequestClose", CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeEditControl_RequestClose(IntPtr hwnd);

        /// <summary>Reads filePath (native file I/O) and populates the control - including, for
        /// C/C++ source, a cpptools-derived outline - entirely in native code. filePathLength is
        /// explicit (native never assumes filePath is null-terminated, even though the P/Invoke
        /// marshaler does null-terminate a `string` parameter) - pass filePath.Length.</summary>
        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_Load", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeEditControl_Load(IntPtr hwnd, string filePath, UIntPtr filePathLength);

        /// <summary>Writes the control's current text to filePath (native file I/O). Same
        /// filePathLength contract as <see cref="NativeEditControl_Load"/>.</summary>
        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_Save", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeEditControl_Save(IntPtr hwnd, string filePath, UIntPtr filePathLength);

        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_IsDirty", CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeEditControl_IsDirty(IntPtr hwnd);

        /// <summary>Generic dispatch for editor-level editing commands (Undo, Redo, Cut, Copy,
        /// Paste, Find, Replace, GotoLine) - see NativeEditor.h's EditorCommand/
        /// EditorCommandArgs. flags is a reserved bitmask (currently unused by any command).
        /// Passed by ref rather than as an IntPtr the caller marshals manually - the CLR pins a
        /// ref struct argument and passes its address, matching the native side's
        /// `const EditorCommandArgs*` exactly.</summary>
        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_ExecCommand", CallingConvention = CallingConvention.StdCall)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool NativeEditControl_ExecCommand(IntPtr hwnd, EditorCommand command, uint flags, ref EditorCommandArgs args);

        /// <summary>Connects everything this host offers NativeEditControls.dll in one call - see
        /// <see cref="HostServices"/> and HostConnection. Copied before this returns; the delegates the
        /// table's pointers came from must stay alive for as long as they are registered.</summary>
        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_SetHost", CallingConvention = CallingConvention.StdCall)]
        public static extern void NativeEditControl_SetHost(ref HostServices services);

        /// <summary>Disconnects everything (a null table).</summary>
        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_SetHost", CallingConvention = CallingConvention.StdCall)]
        public static extern void NativeEditControl_SetHost(IntPtr services);

        /// <summary>Answers a HostGetTextCallback request (from any thread). textLength (in chars) is
        /// explicit; text may be null when status is not Ok.</summary>
        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_HostGetTextReply", CallingConvention = CallingConvention.StdCall)]
        public static extern void NativeEditControl_HostGetTextReply(ulong requestId, int status,
            [MarshalAs(UnmanagedType.LPWStr)] string text, UIntPtr textLength, ulong version);

        /// <summary>Answers a HostApplyEditsCallback request (from any thread).</summary>
        [DllImport("NativeEditControls.dll", EntryPoint = "NativeEditControl_HostApplyEditsReply", CallingConvention = CallingConvention.StdCall)]
        public static extern void NativeEditControl_HostApplyEditsReply(ulong requestId, int status);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool DestroyWindow(IntPtr hWnd);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int nWidth, int nHeight, [MarshalAs(UnmanagedType.Bool)] bool bRepaint);

        [DllImport("user32.dll")]
        public static extern IntPtr SetFocus(IntPtr hWnd);

        
    }
}
