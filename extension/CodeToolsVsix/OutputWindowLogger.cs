using System;
using System.Runtime.InteropServices;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell.Interop;

namespace CodeToolsVsix
{
    /// <summary>
    /// Creates a dedicated "codetools++" pane in VS's Output window and registers it as
    /// NativeEditControl.dll's log sink (see NativeMethods.NativeEditControl_SetLogSink), so
    /// native log output - including cpptools's own internal logging, bubbled up via
    /// CppEditorControl.cpp's EnsureCpptoolsLogSinkRegistered - is visible even when nothing's
    /// attached to catch OutputDebugString (the always-on baseline Logging.cpp already provides
    /// on the native side). This class only adds that second destination; it doesn't replace the
    /// native baseline.
    /// </summary>
    internal static class OutputWindowLogger
    {
        private static readonly Guid PaneGuid = new Guid("c9a1f5d1-6b8e-4b3a-9a7c-6e4f1a2d8b3f");

        private static IVsOutputWindowPane _pane;

        // Held as a static field, not a local variable at the call site - an
        // [UnmanagedFunctionPointer] delegate referenced only by the unmanaged side (which just
        // holds a raw function pointer, invisible to the GC) is eligible for collection the
        // moment nothing managed still references it. That would leave NativeEditControl.dll
        // holding a dangling function pointer - a crash on the next log call, not an immediate
        // failure, so it'd be easy to miss until it happened. Keeping this alive for the
        // process's lifetime is what avoids that.
        private static readonly LogSinkCallback SinkDelegate = OnNativeLog;

        /// <summary>Creates the "codetools++" Output window pane and registers the native log
        /// sink. Call once, from the package's InitializeAsync, on the UI thread - like most VS
        /// shell services, SVsOutputWindow is UI-thread-affine.</summary>
        public static void Initialize(IServiceProvider serviceProvider)
        {
            if (!(serviceProvider.GetService(typeof(SVsOutputWindow)) is IVsOutputWindow outputWindow))
            {
                return;
            }

            Guid paneGuid = PaneGuid;
            if (outputWindow.CreatePane(ref paneGuid, "codetools++", 1, 1) != VSConstants.S_OK)
            {
                return;
            }

            outputWindow.GetPane(ref paneGuid, out _pane);

            NativeMethods.NativeEditControl_SetLogSink(SinkDelegate);
        }

        private static void OnNativeLog(Severity severity, IntPtr message, UIntPtr messageLength)
        {
            if (_pane == null || message == IntPtr.Zero)
            {
                return;
            }

            string text = Marshal.PtrToStringUni(message, (int)messageLength);
            _pane.OutputStringThreadSafe(text + "\r\n");
        }
    }
}
