using CodeToolsVsix;
using Microsoft.VisualStudio.Settings;
using Microsoft.VisualStudio.Shell;
using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Threading.Tasks;

public class OptionsStorage
{
	private readonly ISettingsManager _settingsManager;
    private readonly ISettingsSubset _settingsSubset;
    string[] settingsNames = { "CodeTools.cppExtensions" };
    public OptionsStorage(ISettingsManager settingsManager)
	{
		_settingsManager = settingsManager;
        _settingsSubset = _settingsManager.GetSubset("CodeTools.*");
        _settingsSubset.SettingChangedAsync += OnSettingChangedAsync;
    }

    private  Task OnSettingChangedAsync(object sender, PropertyChangedEventArgs args)
    {
        if (settingsNames.Contains(args.PropertyName))
        {
            string val = _settingsManager.GetValueOrDefault<string>(args.PropertyName, defaultValue: "");
            NativeMethods.NativeEditControl_SettingChanged(args.PropertyName, val);
        }
        return Task.CompletedTask;
    }

    public string cppExtensions()
	{
		// Fetch values mapping precisely to the JSON property identifier keys
		return _settingsManager.GetValueOrDefault<string>("CodeTools.cppExtensions", defaultValue: ".cpp;.cc;.cxx");
	}

}