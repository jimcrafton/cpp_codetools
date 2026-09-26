#include <lex/json5_hash.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace lex {
namespace json5 {

bool parseNumberValue(std::wstring_view text, double& out) {
    std::size_t i = 0;
    bool negative = false;
    if (i < text.size() && (text[i] == L'+' || text[i] == L'-')) {
        negative = text[i] == L'-';
        ++i;
    }
    const std::wstring_view rest = text.substr(i);

    double value = 0;
    if (rest == L"Infinity") {
        value = std::numeric_limits<double>::infinity();
    } else if (rest == L"NaN") {
        value = std::numeric_limits<double>::quiet_NaN();
    } else if (rest.size() > 2 && rest[0] == L'0' && (rest[1] == L'x' || rest[1] == L'X')) {
        for (std::size_t k = 2; k < rest.size(); ++k) {
            const wchar_t c = rest[k];
            int d;
            if (c >= L'0' && c <= L'9') d = c - L'0';
            else if (c >= L'a' && c <= L'f') d = c - L'a' + 10;
            else if (c >= L'A' && c <= L'F') d = c - L'A' + 10;
            else return false;
            value = value * 16 + d;
        }
    } else {
        std::string narrow;
        narrow.reserve(rest.size());
        for (wchar_t c : rest) {
            if (c > 0x7F) return false;
            narrow += static_cast<char>(c);
        }
        if (narrow.empty()) return false;
        // Locale-independent; accepts ".5", "5.", "1e-3".
        const auto r = std::from_chars(narrow.data(), narrow.data() + narrow.size(), value);
        if (r.ec != std::errc() || r.ptr != narrow.data() + narrow.size()) return false;
    }
    out = negative ? -value : value;
    return true;
}

namespace {

constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ull;

std::uint64_t mix(std::uint64_t x) noexcept {
    x += kGolden;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

std::uint64_t combine(std::uint64_t seed, std::uint64_t v) noexcept {
    return mix(seed ^ (mix(v) + kGolden + (seed << 6) + (seed >> 2)));
}

std::uint64_t hashString(std::wstring_view s) noexcept {
    std::uint64_t h = 14695981039346656037ull;  // FNV-1a
    for (wchar_t c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return mix(h ^ s.size());
}

enum Tag : std::uint64_t {
    TagNull = 1, TagBool, TagNumber, TagNumberText, TagString, TagObject, TagArray, TagProperty, TagMissing
};

std::uint64_t hashPrimitive(const ASTNode& n) {
    switch (n.primitive) {
    case PrimitiveKind::Null:
        return combine(TagNull, 0);
    case PrimitiveKind::Boolean:
        return combine(TagBool, n.text() == L"true" ? 1 : 0);
    case PrimitiveKind::String:
        return combine(TagString, hashString(decodeString(n.text())));
    case PrimitiveKind::Number: {
        double d;
        if (!parseNumberValue(n.text(), d)) return combine(TagNumberText, hashString(n.text()));
        std::uint64_t bits;
        if (std::isnan(d)) bits = 0x7FF8000000000000ull;  // every NaN is the same value
        else if (d == 0) bits = 0;                        // and 0 == -0
        else std::memcpy(&bits, &d, sizeof bits);
        return combine(TagNumber, bits);
    }
    default:
        return combine(TagMissing, 0);
    }
}

std::uint64_t hashNode(ASTNode& n) {
    std::uint64_t h = 0;
    switch (n.type) {
    case ASTNodeType::CommentLine:
    case ASTNodeType::CommentBlock:
        n.structuralHash = 0;
        return 0;

    case ASTNodeType::Primitive:
        h = hashPrimitive(n);
        break;

    case ASTNodeType::Property: {
        std::uint64_t value = combine(TagMissing, 0);  // a property with no value (parse error)
        for (const ASTNodePtr& c : n.children())
            if (!c->isComment()) value = hashNode(*c);
        h = combine(combine(TagProperty, hashString(n.key)), value);
        break;
    }

    case ASTNodeType::Object: {
        std::uint64_t sum = 0;  // commutative: member order doesn't matter
        std::uint64_t count = 0;
        for (const ASTNodePtr& c : n.children()) {
            const std::uint64_t hc = hashNode(*c);
            if (c->isComment()) continue;
            sum += mix(hc);
            ++count;
        }
        h = combine(combine(TagObject, count), sum);
        break;
    }

    case ASTNodeType::Array: {
        std::uint64_t count = 0;
        h = combine(TagArray, 0);
        for (const ASTNodePtr& c : n.children()) {
            const std::uint64_t hc = hashNode(*c);
            if (c->isComment()) continue;
            h = combine(h, hc);
            ++count;
        }
        h = combine(h, count);
        break;
    }
    }
    n.structuralHash = static_cast<std::size_t>(h);
    return h;
}

}  // namespace

void computeStructuralHashes(ASTNode& root) { hashNode(root); }

}  // namespace json5
}  // namespace lex
