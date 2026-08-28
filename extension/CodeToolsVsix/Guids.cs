using System;

namespace CodeToolsVsix
{
    /// <summary>Well-known GUIDs shared between the package registration attributes and the runtime code.</summary>
    internal static class PackageGuids
    {
        public const string PackageGuidString = "c436d6f9-fe6c-440c-9aa8-8313f1c8936b";
        public static readonly Guid Package = new Guid(PackageGuidString);
    }

    internal static class EditorGuids
    {
        /// <summary>CLSID of <see cref="CodeToolsEditorFactory"/>, as registered with ProvideEditorFactory.</summary>
        public const string FactoryGuidString = "baa79c8f-6f59-491a-a2ad-f3e749184e4e";
        public static readonly Guid Factory = new Guid(FactoryGuidString);
    }
}
