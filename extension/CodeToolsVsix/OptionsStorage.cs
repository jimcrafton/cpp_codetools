using CodeToolsVsix;
using Microsoft.VisualStudio.Settings;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading;

/// <summary>The codetools++ options (Tools > Options), read from VS's user settings file (settings.json
/// in the instance's data folder - the Options page writes there, and neither SettingChangedAsync nor the
/// ISettingsManager store reflects it) and pushed to NativeEditControls.dll as text: every known key when
/// the host connects (<see cref="PushAll"/>), then whatever changed when a FileSystemWatcher sees the file
/// written. The native side knows each key's type and parses it.</summary>
public class OptionsStorage
{
    private const string Prefix = "CodeTools.";
    private const int DebounceMs = 200;   // a save raises several file events; act once

    // Every key in CodeToolsSettings.registration.json with its declared type (the value's) and registered
    // default. A new setting is an entry there and one here.
    private static readonly Dictionary<string, object> Defaults = new Dictionary<string, object>
    {
        { "CodeTools.cppExtensions", ".cpp;.cc;.cxx" },
        { "CodeTools.editor.highlightDelayMs", 50 },
        { "CodeTools.editor.diagnosticsDelayMs", 600 },
        { "CodeTools.editor.wordWrap", false },
        { "CodeTools.editor.inactiveNotes", true },
        { "CodeTools.editor.fadeInactive", true },
        { "CodeTools.editor.fadeStrength", 70 },
        { "CodeTools.editor.peekTabWidth", 4 },
        { "CodeTools.editor.minimapWidth", 28 },
        { "CodeTools.designer.toolboxWidth", 220 },
        { "CodeTools.designer.propertiesWidth", 300 },
        { "CodeTools.designer.canvasWidth", 640 },
        { "CodeTools.designer.newControlWidth", 120 },
        { "CodeTools.designer.handleSize", 7 },
        { "CodeTools.explorer.hideNative", true },
        { "CodeTools.explorer.defaultView", "Symbols" },
        { "CodeTools.explorer.compileCommandsPaths", "build-ninja;build;out/build/*" },
        { "CodeTools.explorer.cacheCompileDatabase", true },
        { "CodeTools.explorer.dimUnreferenced", true },
    };

    /// <summary>Raised when a CodeTools.explorer.* setting changes - the managed side of the explorer
    /// (hiding the built-in Solution Explorer) acts on those; native hears about them like any other.
    /// May be raised on any thread.</summary>
    public event Action ExplorerSettingChanged;

    private readonly ISettingsManager _settingsManager;   // fallback for a value the file doesn't hold
    private readonly string _settingsPath;                // null when the instance folder can't be found
    private readonly FileSystemWatcher _watcher;          // kept alive by the field
    private readonly Timer _debounce;
    private readonly Dictionary<string, string> _lastPushed = new Dictionary<string, string>();
    private readonly object _fileLock = new object();
    private Dictionary<string, string> _fileValues = new Dictionary<string, string>();
    private DateTime _fileStamp = DateTime.MinValue;
    private long _fileLength = -1;
    private readonly HashSet<string> _reported = new HashSet<string>();

    public OptionsStorage(ISettingsManager settingsManager)
    {
        _settingsManager = settingsManager;
        _settingsPath = FindSettingsFile();
        _debounce = new Timer(_ => Poll(), null, Timeout.Infinite, Timeout.Infinite);
        if (_settingsPath != null)
        {
            try
            {
                _watcher = new FileSystemWatcher(Path.GetDirectoryName(_settingsPath), Path.GetFileName(_settingsPath))
                {
                    NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName,
                };
                FileSystemEventHandler onChange = (s, e) => _debounce.Change(DebounceMs, Timeout.Infinite);
                _watcher.Changed += onChange;
                _watcher.Created += onChange;
                _watcher.Renamed += (s, e) => _debounce.Change(DebounceMs, Timeout.Infinite);   // saved by rename
                _watcher.EnableRaisingEvents = true;
            }
            catch (Exception ex)
            {
                OutputWindowLogger.Write("options: watching settings.json failed: " + ex.Message);
            }
        }
    }

    // The Options page's file: settings.json in the data folder of this Visual Studio instance. VS's
    // AppDataDir may be the roaming copy of that folder while the Options page writes to the local one,
    // so both are tried and the one that exists is used.
    private static string FindSettingsFile()
    {
        ThreadHelper.ThrowIfNotOnUIThread();
        var candidates = new List<string>();
        try
        {
            var shell = ServiceProvider.GlobalProvider.GetService(typeof(SVsShell)) as IVsShell;
            if (shell != null
                && shell.GetProperty((int)__VSSPROPID.VSSPROPID_AppDataDir, out object dir) == 0
                && dir is string folder)
            {
                candidates.Add(Path.Combine(folder, "settings.json"));
                string leaf = new DirectoryInfo(folder).Name;
                string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                candidates.Add(Path.Combine(local, "Microsoft", "VisualStudio", leaf, "settings.json"));
            }
        }
        catch (Exception ex)
        {
            OutputWindowLogger.Write("options: instance folder lookup failed: " + ex.Message);
        }
        string found = candidates.Find(File.Exists);
        OutputWindowLogger.Write(found != null
            ? "options: reading " + found
            : "options: settings.json not found (tried " + string.Join(" | ", candidates) + "), so registered defaults apply");
        return found;
    }

    // Re-reads the file when its timestamp or size changed. The file starts with a comment line.
    private void RefreshFile()
    {
        if (_settingsPath == null)
        {
            return;
        }
        lock (_fileLock)
        {
            try
            {
                var info = new FileInfo(_settingsPath);
                if (!info.Exists)
                {
                    _fileValues = new Dictionary<string, string>();
                    return;
                }
                if (info.LastWriteTimeUtc == _fileStamp && info.Length == _fileLength)
                {
                    return;
                }
                string text;
                using (var stream = new FileStream(_settingsPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (var reader = new StreamReader(stream))
                {
                    text = reader.ReadToEnd();
                }
                text = Regex.Replace(text, @"/\*.*?\*/", string.Empty, RegexOptions.Singleline);
                // The file is a flat object: "CodeTools.key": true | 12 | "text". The raw value text is kept.
                var values = new Dictionary<string, string>();
                foreach (Match m in Regex.Matches(text, @"""(CodeTools\.[^""]+)""\s*:\s*(""(?:[^""\\]|\\.)*""|[^,}\s]+)"))
                {
                    values[m.Groups[1].Value] = m.Groups[2].Value;
                }
                _fileValues = values;
                _fileStamp = info.LastWriteTimeUtc;
                _fileLength = info.Length;
            }
            catch (Exception ex)
            {
                // Often a half-written file; the stamp stays unchanged so the next tick tries again.
                ReportOnce("options: reading settings.json failed: " + ex.Message);
            }
        }
    }

    private void Poll()
    {
        try
        {
            foreach (string key in Defaults.Keys)
            {
                Changed(key);
            }
        }
        catch (Exception ex)
        {
            ReportOnce("options poll failed: " + ex.Message);
        }
    }

    // The setting's value as its declared type: the file's, else the store's, else the registered default.
    private object ReadValue(string key)
    {
        if (!Defaults.TryGetValue(key, out object fallback))
        {
            return null;
        }
        RefreshFile();
        string raw;
        lock (_fileLock) { _fileValues.TryGetValue(key, out raw); }
        try
        {
            switch (fallback)
            {
                case bool b: return raw != null ? bool.Parse(raw) : _settingsManager.GetValueOrDefault<bool>(key, b);
                case int i: return raw != null ? int.Parse(raw, CultureInfo.InvariantCulture) : _settingsManager.GetValueOrDefault<int>(key, i);
                default: return raw != null ? Unquote(raw) : _settingsManager.GetValueOrDefault<string>(key, (string)fallback);
            }
        }
        catch (Exception ex)
        {
            ReportOnce("options read " + key + " failed: " + ex.GetType().Name + " " + ex.Message);
            return fallback;
        }
    }

    private static string Unquote(string raw)
    {
        return raw.Length >= 2 && raw[0] == '"' ? Regex.Unescape(raw.Substring(1, raw.Length - 2)) : raw;
    }

    private void ReportOnce(string text)
    {
        lock (_reported)
        {
            if (!_reported.Add(text)) { return; }
        }
        OutputWindowLogger.Write(text);
    }

    private void Changed(string name)
    {
        if (!Push(name))
        {
            return;
        }
        if (name.StartsWith(Prefix + "explorer.", StringComparison.Ordinal))
        {
            ExplorerSettingChanged?.Invoke();
        }
    }

    /// <summary>Whether the built-in Solution Explorer is hidden while the C++ project explorer is in use.</summary>
    public bool HideNative()
    {
        return (bool)ReadValue("CodeTools.explorer.hideNative");
    }

    /// <summary>Sends every known setting's current value; called once the native side is connected.</summary>
    public void PushAll()
    {
        lock (_lastPushed) { _lastPushed.Clear(); }   // resend all, even what the poll already sent
        try
        {
            foreach (string key in Defaults.Keys)
            {
                Push(key);
            }
        }
        catch (Exception ex)
        {
            OutputWindowLogger.Write("options: pushing the settings failed: " + ex);
        }
    }

    /// <summary>Sends one setting's current value if it differs from what was last sent; false if nothing went.</summary>
    private bool Push(string name)
    {
        object value = ReadValue(name);
        if (value == null)
        {
            return false;
        }
        string text = ToText(value);
        lock (_lastPushed)
        {
            if (_lastPushed.TryGetValue(name, out string last) && last == text)
            {
                return false;
            }
            _lastPushed[name] = text;
        }
        if (name.EndsWith("cacheCompileDatabase", StringComparison.Ordinal))
        {
            OutputWindowLogger.Write("options push " + name + " = " + text);
        }
        try
        {
            NativeMethods.NativeEditControl_SettingChanged(name, text);
        }
        catch (Exception ex) when (ex is DllNotFoundException || ex is EntryPointNotFoundException)
        {
            // The native DLL isn't loadable here; nothing to tell.
        }
        return true;
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
        return (string)ReadValue("CodeTools.cppExtensions");
    }
}
