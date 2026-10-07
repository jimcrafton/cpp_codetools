using CodeToolsVsix;
using Microsoft.VisualStudio.Settings;
using System;
using System.ComponentModel;
using System.Globalization;
using System.Threading.Tasks;

/// <summary>The codetools++ options (Tools > Options), read from the VS settings store and pushed to
/// NativeEditControls.dll as text: once per known key when the host connects (<see cref="PushAll"/>),
/// then on every change under CodeTools.*. The native side knows each key's type and parses it.</summary>
public class OptionsStorage
{
    private const string Prefix = "CodeTools.";

    // Every key in CodeToolsSettings.registration.json, for the first push. A new setting is an entry
    // there and one here; later changes are forwarded for any CodeTools.* key without a list.
    private static readonly string[] Keys =
    {
        "CodeTools.cppExtensions",
        "CodeTools.editor.highlightDelayMs",
        "CodeTools.editor.diagnosticsDelayMs",
        "CodeTools.editor.wordWrap",
        "CodeTools.editor.inactiveNotes",
        "CodeTools.editor.fadeInactive",
        "CodeTools.editor.fadeStrength",
        "CodeTools.editor.peekTabWidth",
        "CodeTools.editor.minimapWidth",
        "CodeTools.designer.toolboxWidth",
        "CodeTools.designer.propertiesWidth",
        "CodeTools.designer.canvasWidth",
        "CodeTools.designer.newControlWidth",
        "CodeTools.designer.handleSize",
        "CodeTools.explorer.defaultView",
        "CodeTools.explorer.cacheCompileDatabase",
    };

    /// <summary>Raised when a CodeTools.explorer.* setting changes - the managed side of the explorer
    /// (hiding the built-in Solution Explorer) acts on those; native hears about them like any other.
    /// May be raised on any thread.</summary>
    public event Action ExplorerSettingChanged;

    private readonly ISettingsManager _settingsManager;
    private readonly ISettingsSubset _settingsSubset;

    public OptionsStorage(ISettingsManager settingsManager)
    {
        _settingsManager = settingsManager;
        _settingsSubset = _settingsManager.GetSubset(Prefix + "*");
        _settingsSubset.SettingChangedAsync += OnSettingChangedAsync;
    }

    private Task OnSettingChangedAsync(object sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName != null && args.PropertyName.StartsWith(Prefix, StringComparison.Ordinal))
        {
            Push(args.PropertyName);
            if (args.PropertyName.StartsWith(Prefix + "explorer.", StringComparison.Ordinal))
            {
                ExplorerSettingChanged?.Invoke();
            }
        }
        return Task.CompletedTask;
    }

    /// <summary>Whether the built-in Solution Explorer is hidden while the C++ project explorer is in use.</summary>
    public bool HideNative()
    {
        return _settingsManager.GetValueOrDefault<bool>("CodeTools.explorer.hideNative", defaultValue: true);
    }

    /// <summary>Sends every known setting's current value; called once the native side is connected.</summary>
    public void PushAll()
    {
        foreach (string key in Keys)
        {
            Push(key);
        }
    }

    private void Push(string name)
    {
        object value = _settingsManager.GetValueOrDefault<object>(name, null);
        if (value == null)
        {
            return;
        }
        try
        {
            NativeMethods.NativeEditControl_SettingChanged(name, ToText(value));
        }
        catch (Exception ex) when (ex is DllNotFoundException || ex is EntryPointNotFoundException)
        {
            // The native DLL isn't loadable here; nothing to tell.
        }
    }

    private static string ToText(object value)
    {
        if (value is bool flag)
        {
            return flag ? "true" : "false";
        }
        return Convert.ToString(value, CultureInfo.InvariantCulture) ?? string.Empty;
    }

    public string cppExtensions()
    {
        return _settingsManager.GetValueOrDefault<string>("CodeTools.cppExtensions", defaultValue: ".cpp;.cc;.cxx");
    }
}
