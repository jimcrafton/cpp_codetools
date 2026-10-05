using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using Microsoft.VisualStudio.Shell;

namespace CodeToolsVsix
{
    /// <summary>The C++ project explorer's tool window: like Solution Explorer, one pane for the whole
    /// session that VS docks, persists and shows or hides. It holds no document - the pane inside it is
    /// native (NativeEditControls.dll's ProjectExplorer) and gets the open workspace pushed to it by
    /// <see cref="WorkspaceTracker"/>, so this class only hosts that window.</summary>
    [Guid(WindowGuidString)]
    public sealed class ProjectExplorerToolWindow : ToolWindowPane
    {
        public const string WindowGuidString = "7b1f8a52-3c4d-4e69-9a0b-5d2e6f9c1a37";

        private NativeToolHost _host;

        public ProjectExplorerToolWindow() : base(null)
        {
            Caption = "C++ Project Explorer";
        }

        /// <summary>The control VS parents into the tool window frame. Created here, on first use, not in
        /// the constructor: VS creates the pane before it has a window to give it.</summary>
        public override IWin32Window Window => _host ?? (_host = new NativeToolHost(ToolWindowType.ProjectExplorer));

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _host?.Dispose();
                _host = null;
            }
            base.Dispose(disposing);
        }
    }

    /// <summary>A plain control whose only job is to be the parent of a native tool-window pane and keep it
    /// the size of itself. The native window is created when this one gets its handle, and closed through
    /// NativeEditControls.dll when this is disposed - never with DestroyWindow, which belongs to the thread
    /// that made it (see NativeToolWindow_Create).</summary>
    internal sealed class NativeToolHost : Control
    {
        private readonly ToolWindowType _type;
        private IntPtr _child;

        public NativeToolHost(ToolWindowType type)
        {
            _type = type;
            SetStyle(ControlStyles.ContainerControl, true);
            TabStop = true;
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            EnsureChild();
        }

        private void EnsureChild()
        {
            if (_child != IntPtr.Zero || !IsHandleCreated)
            {
                return;
            }

            // What the host offers native (opening a file at a line, the log) goes over once per process.
            HostConnection.EnsureRegistered();
            _child = NativeMethods.NativeToolWindow_Create(Handle, 0, 0, Math.Max(ClientSize.Width, 1), Math.Max(ClientSize.Height, 1), _type);
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            if (_child != IntPtr.Zero)
            {
                NativeMethods.NativeToolWindow_SetBounds(_child, 0, 0, Math.Max(ClientSize.Width, 1), Math.Max(ClientSize.Height, 1));
            }
        }

        protected override void OnGotFocus(EventArgs e)
        {
            base.OnGotFocus(e);
            if (_child != IntPtr.Zero)
            {
                NativeMethods.SetFocus(_child);
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (_child != IntPtr.Zero)
            {
                NativeMethods.NativeToolWindow_RequestClose(_child);
                _child = IntPtr.Zero;
            }
            base.Dispose(disposing);
        }
    }
}
