using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.ComponentModelHost;
using Microsoft.VisualStudio.Editor;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.TextManager.Interop;

namespace CodeToolsVsix
{
    /// <summary>Mirrors DocumentEditService.h's EditStatus. Explicit values on both sides.</summary>
    internal enum EditStatus : int
    {
        Ok = 0,
        NotOpen = 1,
        NotFound = 2,
        VersionMismatch = 3,
        InvalidEdit = 4,
        IoError = 5,
        Rejected = 6,
    }

    /// <summary>Mirrors HostEditorBridge.h's HostTextEdit.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct HostTextEdit
    {
        public ulong Offset;
        public ulong Length;
        public IntPtr Text;        // UTF-16, valid only during the callback
        public ulong TextLength;   // in chars
    }

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void HostGetTextCallback(ulong requestId, IntPtr path, UIntPtr pathLength);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void HostApplyEditsCallback(ulong requestId, IntPtr path, UIntPtr pathLength,
        ulong expectedVersion, IntPtr edits, UIntPtr editCount);

    /// <summary>Answers NativeEditControls.dll's requests to read or edit a file that is open in
    /// VS's own text editor: the native Designer plans edits against the live buffer and applies
    /// them here (through the buffer's own undo history), instead of touching the file on disk under
    /// an open document. Anything not open in a VS text buffer is answered NotOpen and native falls
    /// back to the file. A file open in our own editor never reaches here - native serves it itself
    /// (DocumentEditService's registry is consulted first).
    ///
    /// Every request is answered exactly once, and never synchronously from the callback: the
    /// callbacks run on native's edit thread, which must not wait for VS's UI thread (the UI thread
    /// can be blocked waiting on the edit thread), so the work hops to the UI thread and replies
    /// through NativeEditControl_HostGetTextReply / HostApplyEditsReply when done.
    /// Text and offsets crossing the boundary are UTF-16, the buffer's own coordinates.</summary>
    internal static class HostDocumentEditor
    {
        // Native keeps these function pointers, so the delegates must stay alive for good.
        private static readonly HostGetTextCallback GetTextCallback = OnGetText;
        private static readonly HostApplyEditsCallback ApplyEditsCallback = OnApplyEdits;
        private static int _registered;

        public static void EnsureRegistered()
        {
            if (Interlocked.Exchange(ref _registered, 1) == 0)
            {
                NativeMethods.NativeEditControl_SetHostEditor(GetTextCallback, ApplyEditsCallback);
            }
        }

        private static void OnGetText(ulong requestId, IntPtr path, UIntPtr pathLength)
        {
            string filePath = Marshal.PtrToStringUni(path, (int)pathLength);  // copy before returning
            RunOnUiThread(() =>
            {
                ThreadHelper.ThrowIfNotOnUIThread();
                EditStatus status = TryGetBuffer(filePath, out ITextBuffer buffer);
                if (status != EditStatus.Ok)
                {
                    NativeMethods.NativeEditControl_HostGetTextReply(requestId, (int)status, null, UIntPtr.Zero, 0);
                    return;
                }
                ITextSnapshot snapshot = buffer.CurrentSnapshot;
                string text = snapshot.GetText();
                NativeMethods.NativeEditControl_HostGetTextReply(requestId, (int)EditStatus.Ok, text,
                    (UIntPtr)text.Length, (ulong)snapshot.Version.VersionNumber);
            },
            () => NativeMethods.NativeEditControl_HostGetTextReply(requestId, (int)EditStatus.Rejected, null, UIntPtr.Zero, 0));
        }

        private static void OnApplyEdits(ulong requestId, IntPtr path, UIntPtr pathLength, ulong expectedVersion,
            IntPtr edits, UIntPtr editCount)
        {
            // Copy everything now - the native buffers are only valid during this call.
            string filePath = Marshal.PtrToStringUni(path, (int)pathLength);
            var copied = new List<Utf16Edit>((int)editCount);
            int stride = Marshal.SizeOf<HostTextEdit>();
            for (int i = 0; i < (int)editCount; i++)
            {
                var raw = Marshal.PtrToStructure<HostTextEdit>(IntPtr.Add(edits, i * stride));
                string text = raw.TextLength == 0 ? string.Empty : Marshal.PtrToStringUni(raw.Text, (int)raw.TextLength);
                copied.Add(new Utf16Edit((int)raw.Offset, (int)raw.Length, text));
            }

            RunOnUiThread(
                () =>
                {
                    ThreadHelper.ThrowIfNotOnUIThread();
                    NativeMethods.NativeEditControl_HostApplyEditsReply(requestId, (int)Apply(filePath, expectedVersion, copied));
                },
                () => NativeMethods.NativeEditControl_HostApplyEditsReply(requestId, (int)EditStatus.Rejected));
        }

        /// <summary>Runs work on the UI thread; if it throws, runs onFailure instead, so the request
        /// is always answered.</summary>
        private static void RunOnUiThread(Action work, Action onFailure)
        {
            ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
            {
                try
                {
                    await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                    work();
                }
                catch (Exception)
                {
                    onFailure();
                }
            }).Task.FileAndForget("codetools/hosteditor");
        }

        /// <summary>The file's ITextBuffer if it is open in a VS text editor.</summary>
        private static EditStatus TryGetBuffer(string path, out ITextBuffer buffer)
        {
            buffer = null;
            ThreadHelper.ThrowIfNotOnUIThread();

            var rdt = ServiceProvider.GlobalProvider.GetService(typeof(SVsRunningDocumentTable)) as IVsRunningDocumentTable;
            if (rdt == null)
            {
                return EditStatus.NotOpen;
            }

            IntPtr docData = IntPtr.Zero;
            try
            {
                int hr = rdt.FindAndLockDocument((uint)_VSRDTFLAGS.RDT_NoLock, path, out _, out _, out docData, out _);
                if (ErrorHandler.Failed(hr) || docData == IntPtr.Zero)
                {
                    return EditStatus.NotOpen;
                }

                var vsBuffer = Marshal.GetObjectForIUnknown(docData) as IVsTextBuffer;
                if (vsBuffer == null)
                {
                    return EditStatus.NotOpen;  // open, but not in a text buffer (e.g. a designer)
                }

                var model = ServiceProvider.GlobalProvider.GetService(typeof(SComponentModel)) as IComponentModel;
                buffer = model?.GetService<IVsEditorAdaptersFactoryService>()?.GetDocumentBuffer(vsBuffer);
                return buffer != null ? EditStatus.Ok : EditStatus.NotOpen;
            }
            finally
            {
                if (docData != IntPtr.Zero)
                {
                    Marshal.Release(docData);
                }
            }
        }

        private static EditStatus Apply(string path, ulong expectedVersion, List<Utf16Edit> edits)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            EditStatus found = TryGetBuffer(path, out ITextBuffer buffer);
            if (found != EditStatus.Ok)
            {
                return found;
            }
            if (!buffer.CheckEditAccess())
            {
                return EditStatus.Rejected;  // read-only
            }

            ITextSnapshot snapshot = buffer.CurrentSnapshot;
            if ((ulong)snapshot.Version.VersionNumber != expectedVersion)
            {
                return EditStatus.VersionMismatch;
            }
            if (!HostEditPlan.TryOrder(snapshot.GetText(), edits, out List<Utf16Edit> mapped))
            {
                return EditStatus.InvalidEdit;
            }
            if (mapped.Count == 0)
            {
                return EditStatus.Ok;
            }

            // One ITextEdit is one undo step; nothing changes unless every replacement is accepted.
            using (ITextEdit edit = buffer.CreateEdit())
            {
                foreach (Utf16Edit e in mapped)
                {
                    if (!edit.Replace(e.Start, e.Length, e.Text))
                    {
                        edit.Cancel();
                        return EditStatus.Rejected;
                    }
                }
                edit.Apply();
                return edit.Canceled ? EditStatus.Rejected : EditStatus.Ok;
            }
        }
    }
}
