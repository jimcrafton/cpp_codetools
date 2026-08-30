# Rewrites source.extension.vsixmanifest's <Identity Version="..."> attribute to match
# cpp_codetools' own shared VERSION.state - called from CodeToolsVsix.csproj's own
# BuildAndIncludeNativeDependencies Target, before GetVsixSourceItems packages the manifest, so
# VS's Extension Manager ("Manage Extensions" dialog, Extensions > Manage Extensions) shows the
# real build version instead of a permanently-frozen "1.0.0" - which also happened to make
# VSIXInstaller.exe silently no-op a reinstall, since it only reinstalls when Identity Version
# actually changes (see repo CLAUDE.md's own note on this).
#
# A plain string/regex replace over the raw XML risks corrupting the file's *other* Version
# attributes (InstallationTarget/Dependency/Prerequisite all have their own, unrelated Version
# values) - loading it as XML and targeting only the Identity element's attribute avoids that
# entirely. The manifest declares a default XML namespace, so XPath needs an explicit namespace
# manager - a plain unprefixed //Identity XPath silently matches nothing against a
# namespace-qualified document.
param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [Parameter(Mandatory = $true)][string]$Version
)

[xml]$doc = Get-Content -LiteralPath $ManifestPath

$ns = New-Object System.Xml.XmlNamespaceManager($doc.NameTable)
$ns.AddNamespace('d', 'http://schemas.microsoft.com/developer/vsx-schema/2011')

$identity = $doc.SelectSingleNode('//d:PackageManifest/d:Metadata/d:Identity', $ns)
if (-not $identity) {
    throw "UpdateVsixManifestVersion.ps1: couldn't find Metadata/Identity in $ManifestPath"
}

$identity.SetAttribute('Version', $Version)
$doc.Save($ManifestPath)
