using System;
using System.Runtime.InteropServices;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell.Interop;

namespace CodeToolsVsix
{
    /// <summary>
    /// Creates <see cref="CodeToolsEditorPane"/> instances. One factory instance is created by
    /// the package and registered with <c>RegisterEditorFactory</c>; the shell calls
    /// <see cref="CreateEditorInstance"/> once per document window whenever the user opens a
    /// .cpp/.h/... file with this editor (via "Open With..." or as the default, per registration).
    /// </summary>
    [Guid(EditorGuids.FactoryGuidString)]
    internal sealed class CodeToolsEditorFactory : IVsEditorFactory
    {
        private readonly CodeToolsPackage _package;
        private Microsoft.VisualStudio.OLE.Interop.IServiceProvider _oleServiceProvider;

        public CodeToolsEditorFactory(CodeToolsPackage package)
        {
            _package = package ?? throw new ArgumentNullException(nameof(package));
        }

        public int SetSite(Microsoft.VisualStudio.OLE.Interop.IServiceProvider psp)
        {
            _oleServiceProvider = psp;
            return VSConstants.S_OK;
        }

        public int CreateEditorInstance(
            uint grfCreateDoc,
            string pszMkDocument,
            string pszPhysicalView,
            IVsHierarchy pvHier,
            uint itemid,
            IntPtr punkDocDataExisting,
            out IntPtr ppunkDocView,
            out IntPtr ppunkDocData,
            out string pbstrEditorCaption,
            out Guid pguidCmdUI,
            out int pgrfCDW)
        {
            ppunkDocView = IntPtr.Zero;
            ppunkDocData = IntPtr.Zero;
            pbstrEditorCaption = string.Empty;
            // No custom command UI context (no .vsct) - leave the shell's default context in place.
            pguidCmdUI = Guid.Empty;
            pgrfCDW = 0;

            // This sample doesn't support taking over doc data another editor already created
            // in-memory (co-editing the same buffer). Decline; VS will fall back to opening a
            // second, independent copy instead, which is fine for a text file on disk.
            if (punkDocDataExisting != IntPtr.Zero)
            {
                return VSConstants.VS_E_INCOMPATIBLEDOCDATA;
            }

            // CodeToolsEditorPane plays both roles - DocData (persistence) and DocView (the
            // window) - which is the simplest valid shape for a custom editor and is what most
            // non-text custom editor samples do.
            var pane = new CodeToolsEditorPane(pszMkDocument);
            ppunkDocView = Marshal.GetIUnknownForObject(pane);
            ppunkDocData = Marshal.GetIUnknownForObject(pane);

            return VSConstants.S_OK;
        }

        public int Close()
        {
            return VSConstants.S_OK;
        }

        public int MapLogicalView(ref Guid rguidLogicalView, out string pbstrPhysicalView)
        {
            pbstrPhysicalView = null;

            if (rguidLogicalView == VSConstants.LOGVIEWID_Primary ||
                rguidLogicalView == VSConstants.LOGVIEWID_Designer)
            {
                return VSConstants.S_OK;
            }

            return VSConstants.E_NOTIMPL;
        }
    }
}
