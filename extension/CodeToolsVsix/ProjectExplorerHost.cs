using System;
using System.ComponentModel.Design;
using System.Threading.Tasks;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace CodeToolsVsix
{
    /// <summary>Everything that ties the project explorer to the rest of VS: the View > Other Windows
    /// command that shows it, following the open workspace, and hiding the built-in Solution Explorer
    /// (the CodeTools.explorer.hideNative setting).</summary>
    internal sealed class ProjectExplorerHost
    {
        /// <summary>The command set this package's .vsct defines.</summary>
        public static readonly Guid CommandSet = new Guid("c3d2a8f4-6b19-4f07-8e55-1a7b9d4c2e60");
        public const int ShowExplorerCommandId = 0x0100;

        /// <summary>Solution Explorer's tool window GUID (the persistence slot VS knows it by).</summary>
        private static readonly Guid SolutionExplorerSlot = new Guid("3ae79031-e1bc-11d0-8f78-00a0c9110057");

        private readonly AsyncPackage _package;
        private WorkspaceTracker _tracker;
        private bool _hiddenByUs;
        private bool _shownOnce;

        private ProjectExplorerHost(AsyncPackage package)
        {
            _package = package;
        }

        public static async Task<ProjectExplorerHost> CreateAsync(AsyncPackage package)
        {
            await package.JoinableTaskFactory.SwitchToMainThreadAsync();

            var host = new ProjectExplorerHost(package);
            if (await package.GetServiceAsync(typeof(IMenuCommandService)) is OleMenuCommandService commands)
            {
                commands.AddCommand(new MenuCommand((s, e) => host.ShowExplorer(), new CommandID(CommandSet, ShowExplorerCommandId)));
            }

            host._tracker = await WorkspaceTracker.CreateAsync(package);
            host._tracker.RootChanged += host.OnRootChanged;
            if (CodeToolsPackage.Options != null)
            {
                CodeToolsPackage.Options.ExplorerSettingChanged += host.OnSettingChanged;
            }
            host.OnRootChanged(host._tracker.Root);
            return host;
        }

        /// <summary>Brings the explorer to the front, creating it the first time.</summary>
        public void ShowExplorer()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            ToolWindowPane window = _package.FindToolWindow(typeof(ProjectExplorerToolWindow), 0, true);
            if (window?.Frame is IVsWindowFrame frame)
            {
                ErrorHandler.ThrowOnFailure(frame.Show());
            }
        }

        private void OnRootChanged(string root)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (root == null)
            {
                return;
            }
            // With the built-in explorer hidden, ours is the one a user looks at: show it the first time a
            // workspace opens. Later it stays where they left it.
            if (HideNative && !_shownOnce)
            {
                _shownOnce = true;
                ShowExplorer();
            }
            ApplyHideNative();
        }

        private void OnSettingChanged()
        {
            ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                ApplyHideNative();
                if (HideNative && _tracker?.Root != null)
                {
                    ShowExplorer();
                }
            }).FileAndForget("codetools/explorer-setting");
        }

        private static bool HideNative => CodeToolsPackage.Options?.HideNative() ?? true;

        /// <summary>Hides Solution Explorer while the setting says to - Hide(), not close, so it stays
        /// one click away - and shows it again if it was hidden by this and the setting is turned off. It is
        /// found without creating it: a window that does not exist yet has nothing to hide.</summary>
        private void ApplyHideNative()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (!(Package.GetGlobalService(typeof(SVsUIShell)) is IVsUIShell shell))
            {
                return;
            }
            Guid slot = SolutionExplorerSlot;
            if (!ErrorHandler.Succeeded(shell.FindToolWindow(0, ref slot, out IVsWindowFrame frame)) || frame == null)
            {
                return;
            }
            if (HideNative)
            {
                if (frame.IsVisible() == VSConstants.S_OK)
                {
                    frame.Hide();
                    _hiddenByUs = true;
                }
            }
            else if (_hiddenByUs)
            {
                frame.ShowNoActivate();
                _hiddenByUs = false;
            }
        }
    }
}
