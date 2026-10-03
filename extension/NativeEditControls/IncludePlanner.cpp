#include "IncludePlanner.h"

#include <algorithm>
#include <cwctype>

#include "TextEncoding.h"

namespace CodeToolsVsix
{
    namespace
    {
        // "<newui/controls.h>" / "\"a.h\"" -> "newui/controls.h": no delimiters, forward slashes, lower case.
        std::wstring normalizedPath(std::wstring target)
        {
            while (!target.empty() && std::iswspace(target.front()))
            {
                target.erase(target.begin());
            }
            while (!target.empty() && std::iswspace(target.back()))
            {
                target.pop_back();
            }
            if (target.size() >= 2 && ((target.front() == L'<' && target.back() == L'>') ||
                                       (target.front() == L'"' && target.back() == L'"')))
            {
                target = target.substr(1, target.size() - 2);
            }
            for (wchar_t& c : target)
            {
                c = c == L'\\' ? L'/' : static_cast<wchar_t>(std::towlower(c));
            }
            return target;
        }

        struct Line
        {
            std::size_t start = 0;   // first character
            std::size_t end = 0;     // one past the last character before the line ending
            std::size_t next = 0;    // first character of the following line (past the ending)
        };

        std::vector<Line> linesOf(const std::wstring& text)
        {
            std::vector<Line> lines;
            std::size_t start = 0;
            while (start < text.size())
            {
                std::size_t end = text.find_first_of(L"\r\n", start);
                if (end == std::wstring::npos)
                {
                    lines.push_back({ start, text.size(), text.size() });
                    break;
                }
                std::size_t next = end + 1;
                if (text[end] == L'\r' && next < text.size() && text[next] == L'\n')
                {
                    ++next;
                }
                lines.push_back({ start, end, next });
                start = next;
            }
            return lines;
        }

        // For `#  include <x>` / `# include "x"`: the target with its delimiters ("<x>"); empty otherwise.
        std::wstring includeTargetOf(const std::wstring& line)
        {
            std::size_t i = 0;
            auto skipSpaces = [&] { while (i < line.size() && (line[i] == L' ' || line[i] == L'\t')) ++i; };
            skipSpaces();
            if (i >= line.size() || line[i] != L'#')
            {
                return std::wstring();
            }
            ++i;
            skipSpaces();
            if (line.compare(i, 7, L"include") != 0)
            {
                return std::wstring();
            }
            i += 7;
            skipSpaces();
            if (i >= line.size() || (line[i] != L'<' && line[i] != L'"'))
            {
                return std::wstring();
            }
            const wchar_t close = line[i] == L'<' ? L'>' : L'"';
            const std::size_t closing = line.find(close, i + 1);
            return closing == std::wstring::npos ? std::wstring() : line.substr(i, closing - i + 1);
        }

        bool isPragmaOnce(const std::wstring& line)
        {
            std::size_t i = 0;
            while (i < line.size() && (line[i] == L' ' || line[i] == L'\t')) ++i;
            if (i >= line.size() || line[i] != L'#')
            {
                return false;
            }
            ++i;
            while (i < line.size() && (line[i] == L' ' || line[i] == L'\t')) ++i;
            if (line.compare(i, 6, L"pragma") != 0)
            {
                return false;
            }
            i += 6;
            while (i < line.size() && (line[i] == L' ' || line[i] == L'\t')) ++i;
            return line.compare(i, 4, L"once") == 0;
        }
    }

    IncludePlan planIncludes(const std::wstring& content, const std::vector<std::string>& wanted)
    {
        IncludePlan plan;
        const std::vector<Line> lines = linesOf(content);

        std::vector<std::wstring> present;
        const Line* lastInclude = nullptr;
        const Line* pragmaOnce = nullptr;
        for (const Line& line : lines)
        {
            const std::wstring text = content.substr(line.start, line.end - line.start);
            const std::wstring target = includeTargetOf(text);
            if (!target.empty())
            {
                present.push_back(normalizedPath(target));
                lastInclude = &line;
            }
            else if (pragmaOnce == nullptr && isPragmaOnce(text))
            {
                pragmaOnce = &line;
            }
        }

        for (const std::string& header : wanted)
        {
            const std::wstring key = normalizedPath(utf8ToWide(header));
            if (key.empty() || std::find(present.begin(), present.end(), key) != present.end())
            {
                continue;
            }
            const bool alreadyListed = std::find(plan.missing.begin(), plan.missing.end(), header) != plan.missing.end();
            if (!alreadyListed)
            {
                plan.missing.push_back(header);
            }
        }
        if (plan.missing.empty())
        {
            return plan;
        }

        const std::wstring eol = content.find(L"\r\n") != std::wstring::npos ? L"\r\n" : L"\n";
        std::wstring block;
        for (const std::string& header : plan.missing)
        {
            block += L"#include " + utf8ToWide(header) + eol;
        }

        TextEdit edit;
        if (lastInclude != nullptr)
        {
            edit.offset = lastInclude->next;
            // The last include may be the file's last line, with no ending to keep the new lines apart.
            edit.text = (lastInclude->next == lastInclude->end ? eol : std::wstring()) + block;
        }
        else if (pragmaOnce != nullptr)
        {
            edit.offset = pragmaOnce->next;
            edit.text = (pragmaOnce->next == pragmaOnce->end ? eol : std::wstring()) + eol + block;
        }
        else
        {
            edit.offset = 0;
            edit.text = block + eol;
        }
        plan.edits.push_back(std::move(edit));
        return plan;
    }
}
