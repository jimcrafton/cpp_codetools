using System;
using System.Threading.Tasks;
using EnvDTE;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.ComponentModelHost;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Microsoft.VisualStudio.Workspace.VSIntegration.Contracts;

namespace CodeToolsVsix
{
    /// <summary>Tells the native explorer which folder is open. A solution's root is its directory; in Open
    /// Folder mode it is the workspace's location (IVsFolderWorkspaceService). Pushes the folder and the
    /// active build configuration to NativeEditControls.dll whenever either changes, and raises
    /// <see cref="RootChanged"/> for the managed side (hiding the native Solution Explorer).</summary>
    internal sealed class WorkspaceTracker : IVsSolutionEvents, IDisposable
    {
        private IVsSolution _solution;
        private IVsFolderWorkspaceService _folders;
        private DTE _dte;
        private uint _cookie;

        /// <summary>The open folder (null: nothing is open). Raised on the UI thread.</summary>
        public event Action<string> RootChanged;

        public string Root { get; private set; }

        public static async Task<WorkspaceTracker> CreateAsync(AsyncPackage package)
        {
            await package.JoinableTaskFactory.SwitchToMainThreadAsync();

            var tracker = new WorkspaceTracker
            {
                _solution = await package.GetServiceAsync(typeof(SVsSolution)) as IVsSolution,
                _dte = await package.GetServiceAsync(typeof(SDTE)) as DTE,
            };
            tracker._solution?.AdviseSolutionEvents(tracker, out tracker._cookie);

            if (await package.GetServiceAsync(typeof(SComponentModel)) is IComponentModel components)
            {
                tracker._folders = components.GetService<IVsFolderWorkspaceService>();
                if (tracker._folders != null)
                {
                    tracker._folders.OnActiveWorkspaceChanged += tracker.OnActiveWorkspaceChanged;
                }
            }

            tracker.Push();
            return tracker;
        }

        private Task OnActiveWorkspaceChanged(object sender, EventArgs e)
        {
            return ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                Push();
            }).Task;
        }

        /// <summary>Reads what is open now and tells native (and listeners) if it changed.</summary>
        private void Push()
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            string root = CurrentRoot();
            string configuration = ActiveConfiguration();
            try
            {
                NativeMethods.NativeEditControl_WorkspaceChanged(root ?? string.Empty, configuration ?? string.Empty);
            }
            catch (Exception ex) when (ex is DllNotFoundException || ex is EntryPointNotFoundException)
            {
                // The native DLL isn't loadable here; the explorer can't open anyway.
            }

            if (!string.Equals(root, Root, StringComparison.OrdinalIgnoreCase))
            {
                Root = root;
                RootChanged?.Invoke(root);
            }
        }

        private string CurrentRoot()
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            string folder = _folders?.CurrentWorkspace?.Location;
            if (!string.IsNullOrEmpty(folder))
            {
                return folder;
            }
            if (_solution != null && ErrorHandler.Succeeded(_solution.GetSolutionInfo(out string directory, out _, out _))
                && !string.IsNullOrEmpty(directory))
            {
                return directory.TrimEnd('\\');
            }
            return null;
        }

        private string ActiveConfiguration()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            try
            {
                return _dte?.Solution?.SolutionBuild?.ActiveConfiguration?.Name;
            }
            catch (Exception)
            {
                return null;   // no solution build in Open Folder mode
            }
        }

        public int OnAfterOpenSolution(object pUnkReserved, int fNewSolution) { Push(); return VSConstants.S_OK; }
        public int OnAfterCloseSolution(object pUnkReserved) { Push(); return VSConstants.S_OK; }
        public int OnAfterOpenProject(IVsHierarchy pHierarchy, int fAdded) => VSConstants.S_OK;
        public int OnQueryCloseProject(IVsHierarchy pHierarchy, int fRemoving, ref int pfCancel) => VSConstants.S_OK;
        public int OnBeforeCloseProject(IVsHierarchy pHierarchy, int fRemoved) => VSConstants.S_OK;
        public int OnAfterLoadProject(IVsHierarchy pStubHierarchy, IVsHierarchy pRealHierarchy) => VSConstants.S_OK;
        public int OnQueryUnloadProject(IVsHierarchy pRealHierarchy, ref int pfCancel) => VSConstants.S_OK;
        public int OnBeforeUnloadProject(IVsHierarchy pRealHierarchy, IVsHierarchy pStubHierarchy) => VSConstants.S_OK;
        public int OnQueryCloseSolution(object pUnkReserved, ref int pfCancel) => VSConstants.S_OK;
        public int OnBeforeCloseSolution(object pUnkReserved) => VSConstants.S_OK;

        public void Dispose()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (_cookie != 0 && _solution != null)
            {
                _solution.UnadviseSolutionEvents(_cookie);
                _cookie = 0;
            }
            if (_folders != null)
            {
                _folders.OnActiveWorkspaceChanged -= OnActiveWorkspaceChanged;
            }
        }
    }
}
