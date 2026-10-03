#pragma once

#include <string>
#include <vector>

#include "DocumentEditService.h"

namespace CodeToolsVsix
{
    // Which C++ class holds a .newui's event handlers: the file's top-level
    //   controller: { class: "SaveDialogController", header: "SaveDialogController.h" },
    // (one canonical controller per .newui - see the wiring UX notes). `header` is relative to the
    // .newui's folder, '/'-separated. A Designer save leaves the key alone (it only rewrites the
    // keys it owns), so this is edited as text with the lossless lex parser: everything else in the
    // file - comments, formatting, other keys - stays byte for byte.
    struct ControllerRef
    {
        std::string className;
        std::string header;

        bool operator==(const ControllerRef& other) const { return className == other.className && header == other.header; }
    };

    // The file's controller reference; false if the text has none (or isn't a JSON5 object).
    bool readControllerRef(const std::wstring& newuiText, ControllerRef& out);

    // The UTF-16 edit(s) that make ref the file's controller: replaces an existing `controller`
    // property, or inserts one first thing inside the top-level braces. False if the text isn't a
    // cleanly parseable JSON5 object - a file with syntax errors is never edited (nothing is planned).
    bool planSetControllerRef(const std::wstring& newuiText, const ControllerRef& ref, std::vector<TextEdit>& out);
}
