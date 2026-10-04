using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace CodeToolsVsix
{
    /// <summary>The one place this host tells NativeEditControls.dll what it can do for it: builds the
    /// HostServices table from each capability's callback and registers it with a single call
    /// (NativeEditControl_SetHost). A new capability is one more member of the table (appended in
    /// HostServices.h and NativeMethods.HostServices) and one more line here - not another export.
    ///
    /// Every callback runs on native's edit thread and returns at once, doing the real work on the UI
    /// thread (see HostDocumentEditor / HostDocumentOpener).</summary>
    internal static class HostConnection
    {
        private static int _registered;

        /// <summary>Registers the table, once per process.</summary>
        public static void EnsureRegistered()
        {
            if (Interlocked.Exchange(ref _registered, 1) != 0)
            {
                return;
            }

            var services = new HostServices
            {
                Size = (uint)Marshal.SizeOf<HostServices>(),

                // No managed log sink yet: native logs to stdout, which VS discards. A sink that
                // writes to a VS Output window pane would go here (a LogSinkCallback).
                LogSink = IntPtr.Zero,

                GetText = Marshal.GetFunctionPointerForDelegate(HostDocumentEditor.GetTextCallback),
                ApplyEdits = Marshal.GetFunctionPointerForDelegate(HostDocumentEditor.ApplyEditsCallback),
                OpenLocation = Marshal.GetFunctionPointerForDelegate(HostDocumentOpener.OpenLocationCallback),
            };
            NativeMethods.NativeEditControl_SetHost(ref services);
        }
    }
}
