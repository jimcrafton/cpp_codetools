using System;
using System.Runtime.InteropServices;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace CodeToolsVsix
{
    /// <summary>Answers NativeEditControls.dll's log sink (the HostServices logSink member): every line
    /// native logs - its own and cpptools's - goes to a "codetools++" pane of VS's Output window.
    ///
    /// Like every call from native out to the host it returns at once: the text is copied and the pane
    /// work (creating it needs the UI thread) hops there. Lines from one thread keep their order.</summary>
    internal static class OutputWindowLogger
    {
        private static readonly Guid PaneGuid = new Guid("5d0c3a46-7a0e-4c3f-9a54-2b1d6f0e8c71");
        private const string PaneName = "codetools++";

        // Native keeps this function pointer (HostConnection puts it in the HostServices table), so the
        // delegate must stay alive for good.
        internal static readonly LogSinkCallback LogCallback = OnLog;

        private static IVsOutputWindowPane _pane;   // UI thread only

        private static void OnLog(Severity severity, IntPtr message, UIntPtr messageLength)
        {
            string text = Marshal.PtrToStringUni(message, (int)messageLength);  // copy before returning
            string line = Prefix(severity) + text + Environment.NewLine;

            ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                _pane?.OutputStringThreadSafe(line);
                if (_pane == null && TryCreatePane())
                {
                    _pane.OutputStringThreadSafe(line);
                }
            }).Task.FileAndForget("codetools/outputlogger");
        }

        /// <summary>A line from the managed side, to the same pane.</summary>
        internal static void Write(string text)
        {
            IntPtr ptr = Marshal.StringToHGlobalUni(text);
            try { OnLog(Severity.Warning, ptr, (UIntPtr)text.Length); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        private static string Prefix(Severity severity)
        {
            switch (severity)
            {
                case Severity.Warning: return "warning: ";
                case Severity.Error: return "error: ";
                case Severity.Fatal: return "fatal: ";
                default: return string.Empty;
            }
        }

        private static bool TryCreatePane()
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            var window = ServiceProvider.GlobalProvider.GetService(typeof(SVsOutputWindow)) as IVsOutputWindow;
            if (window == null)
            {
                return false;
            }
            Guid guid = PaneGuid;
            window.CreatePane(ref guid, PaneName, 1, 1);   // visible, cleared with the "Clear all" command
            window.GetPane(ref guid, out _pane);
            return _pane != null;
        }
    }
}
