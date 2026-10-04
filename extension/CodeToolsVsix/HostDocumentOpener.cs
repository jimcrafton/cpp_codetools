using System;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Microsoft.VisualStudio.TextManager.Interop;

namespace CodeToolsVsix
{
    /// <summary>Mirrors HostLocationOpener.h's HostOpenLocationCallback. path is valid only during the
    /// call; line and column are 1-based and the column counts UTF-16 units.</summary>
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void HostOpenLocationCallback(IntPtr path, UIntPtr pathLength, ulong line, ulong column);

    /// <summary>Answers NativeEditControls.dll's "open this file in an editor, at this line" request -
    /// what the Designer's Open in editor does for a problem in its controller's header. Opens the file
    /// the way VS opens any file (its default editor, an already-open document is just brought to the
    /// front) and moves the caret to the line.
    ///
    /// A fire-and-forget request, like every call from native's edit thread out to the host: the
    /// callback runs on that thread, copies the path and returns at once, and the real work hops to the
    /// UI thread (see HostDocumentEditor for why the edit thread must never wait on VS). There is no
    /// reply - if the file can't be opened VS shows its own error, and native has nothing more to do.</summary>
    internal static class HostDocumentOpener
    {
        // Native keeps this function pointer, so the delegate must stay alive for good.
        private static readonly HostOpenLocationCallback OpenLocationCallback = OnOpenLocation;
        private static int _registered;

        public static void EnsureRegistered()
        {
            if (Interlocked.Exchange(ref _registered, 1) == 0)
            {
                NativeMethods.NativeEditControl_SetHostOpenLocation(OpenLocationCallback);
            }
        }

        private static void OnOpenLocation(IntPtr path, UIntPtr pathLength, ulong line, ulong column)
        {
            string filePath = Marshal.PtrToStringUni(path, (int)pathLength);  // copy before returning
            int line0 = line > 0 ? (int)line - 1 : 0;       // VS counts from 0
            int column0 = column > 0 ? (int)column - 1 : 0;

            ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                OpenAt(filePath, line0, column0);
            }).Task.FileAndForget("codetools/hostopener");
        }

        /// <summary>Opens filePath and puts the caret at (line, column), both 0-based, scrolled into view.</summary>
        private static void OpenAt(string filePath, int line, int column)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            // Guid.Empty: the file's default view, in whichever editor VS picks for it.
            VsShellUtilities.OpenDocument(ServiceProvider.GlobalProvider, filePath, Guid.Empty,
                out _, out _, out IVsWindowFrame frame);
            if (frame == null)
            {
                return;
            }
            frame.Show();

            // A text editor has a text view; a document open in one of our own designers does not, and
            // there is no line to go to in it.
            IVsTextView view = VsShellUtilities.GetTextView(frame);
            if (view == null)
            {
                return;
            }
            view.SetCaretPos(line, column);
            view.CenterLines(line, 1);
        }
    }
}
