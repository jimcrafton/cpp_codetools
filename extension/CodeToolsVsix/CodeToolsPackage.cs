using System;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Task = System.Threading.Tasks.Task;
using Microsoft.VisualStudio.Settings;

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
    // .newui is newui's own JSON5-based serialization format for its reflection API (objects
    // read/written through it, not C++ source) - registered here to exercise
    // DocumentType.Designer end to end (see CodeToolsEditorPane.DocumentTypeFromPath), routing to
    // DesignerEditor instead of CppEditor.
    [ProvideEditorExtension(typeof(CodeToolsEditorFactory), ".newui", 100)]
    [ProvideSettingsManifest(PackageRelativeManifestFile = "CodeToolsSettings.registration.json")]
    public sealed class CodeToolsPackage : AsyncPackage
    {
        public static OptionsStorage Options { get; private set; }

        protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
        {
            await base.InitializeAsync(cancellationToken, progress);
            await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);
            
            RegisterEditorFactory(new CodeToolsEditorFactory(this));

            // 1. Fetch the Visual Studio Settings Manager service
            ISettingsManager settingsManager = await GetServiceAsync(typeof(SVsSettingsManager)) as ISettingsManager;

            if (settingsManager != null)
            {
                // 2. Instantiate your storage wrapper
                Options = new OptionsStorage(settingsManager);
            }
        }
    }
}
    