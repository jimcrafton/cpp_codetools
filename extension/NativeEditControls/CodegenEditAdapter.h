#pragma once

#include <string>
#include <vector>

#include "DocumentEditService.h"
#include "cpptools_codegen/delegatewiring.h"

namespace CodeToolsVsix
{
    // The one place UTF-8 (what clang and cpptools_codegen plan in) meets UTF-16 (everything else).
    // Codegen must be given the document as UTF-8 - planningText() - and its edits come back as
    // UTF-8 byte offsets into exactly that text; toTextEdits() turns them into UTF-16 TextEdits
    // against the snapshot. False if any offset is past the end or inside a multi-byte character.
    std::string planningText(const std::wstring& snapshotText);

    bool toTextEdits(const std::wstring& snapshotText,
                     const std::vector<cpptools_codegen::DelegateWiringEdit>& planned,
                     std::vector<TextEdit>& out);
}
