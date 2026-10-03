#include "CodegenEditAdapter.h"

#include "TextEncoding.h"

namespace CodeToolsVsix
{
    std::string planningText(const std::wstring& snapshotText)
    {
        return wideToUtf8(snapshotText);
    }

    bool toTextEdits(const std::wstring& snapshotText,
                     const std::vector<cpptools_codegen::DelegateWiringEdit>& planned,
                     std::vector<TextEdit>& out)
    {
        const std::string utf8 = planningText(snapshotText);
        std::vector<TextEdit> result;
        result.reserve(planned.size());
        for (const cpptools_codegen::DelegateWiringEdit& edit : planned)
        {
            std::size_t offset = 0;
            if (!utf8OffsetToUtf16(utf8, edit.offset, offset))
            {
                return false;
            }
            result.push_back({ offset, 0, utf8ToWide(edit.text) });  // codegen edits are insertions
        }
        out = std::move(result);
        return true;
    }
}
