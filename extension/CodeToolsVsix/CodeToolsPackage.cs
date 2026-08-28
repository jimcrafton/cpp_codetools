using System;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Task = System.Threading.Tasks.Task;

namespace CodeToolsVsix
{
    /// <summary>
    /// Registers <see cref="CodeToolsEditorFactory"/> as an alternate editor for common C/C++
    /// source extensions. Priority 100 is intentionally well above the built-in Source Code
    /// (text) editor's registration, so this does NOT become the default editor for these
    /// extensions - it shows up as a choice in File &gt; Open With..., where it can also be set
    /// as the default per-user/per-extension if desired.
    /// </summary>
    [PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
    [InstalledProductRegistration(
        "codetools++",
        "Registers an alternate editor for C/C++ source files (.cpp/.cc/.cxx/.h/.hpp) that hosts a " +
        "native Win32 HWND control as the text-editing surface, with a symbol outline parsed by " +
        "this repo's own cpptools library entirely on the native side. Available via File > Open With...",
        "1.0")]
    [Guid(PackageGuids.PackageGuidString)]
    [ProvideEditorFactory(typeof(CodeToolsEditorFactory), 110, TrustLevel = __VSEDITORTRUSTLEVEL.ETL_AlwaysTrusted)]
    [ProvideEditorLogicalView(typeof(CodeToolsEditorFactory), VSConstants.LOGVIEWID.Designer_string)]
    [ProvideEditorExtension(typeof(CodeToolsEditorFactory), ".cpp", 100)]
    [ProvideEditorExtension(typeof(CodeToolsEditorFactory), ".cc", 100)]
    [ProvideEditorExtension(typeof(CodeToolsEditorFactory), ".cxx", 100)]
    [ProvideEditorExtension(typeof(CodeToolsEditorFactory), ".h", 100)]
    [ProvideEditorExtension(typeof(CodeToolsEditorFactory), ".hpp", 100)]
    public sealed class CodeToolsPackage : AsyncPackage
    {
        protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
        {
            await base.InitializeAsync(cancellationToken, progress);
            await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);

            OutputWindowLogger.Initialize(this);
            RegisterEditorFactory(new CodeToolsEditorFactory(this));
        }
    }
}
