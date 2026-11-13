#include "core/template_stream.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>

static std::string stream(const std::string& input, const std::map<std::string, std::string>& replacements,
                          size_t& largest) {
    std::string output;
    auto sink = [&](const char* bytes, size_t length) {
        assert(length <= 512);
        largest = std::max(largest, length);
        output.append(bytes, length);
    };
    TemplateStream::Buffer<decltype(sink)> buffer(sink);
    TemplateStream::visit(input.data(), input.size(), [&](const char* bytes, size_t length, bool token) {
        const auto found = token ? replacements.find(std::string(bytes, length)) : replacements.end();
        if (found != replacements.end())
            buffer.append(found->second.data(), found->second.size());
        else
            buffer.append(bytes, length);
    });
    buffer.flush();
    return output;
}
static std::string extract(const std::string& source, const std::string& begin, const std::string& end) {
    const auto at = source.find(begin);
    assert(at != std::string::npos);
    const auto until = source.find(end, at + begin.size());
    assert(until != std::string::npos);
    return source.substr(at + begin.size(), until - at - begin.size());
}
int main() {
    size_t largest = 0;
    assert(stream("%FIRST%%SECOND%", {{"%FIRST%", "A"}, {"%SECOND%", "B"}}, largest) == "AB");
    assert(stream("sent % total; 100%; %UNKNOWN%; %broken", {}, largest) == "sent % total; 100%; %UNKNOWN%; %broken");
    const std::string longText = std::string(20000, 'x') + "%VALUE%" + std::string(20000, 'y');
    assert(stream(longText, {{"%VALUE%", "Русский & <текст>"}}, largest) ==
           std::string(20000, 'x') + "Русский & <текст>" + std::string(20000, 'y'));
    assert(largest == 512);

    const auto root = std::filesystem::path(__FILE__).is_absolute()
                              ? std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
                              : std::filesystem::current_path();
    std::ifstream file(root / "src/net/web_config.cpp");
    assert(file);
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::regex entry(R"TOK(\{\s*"(%[A-Z0-9_]+%)"\s*,\s*MsgId::([A-Z0-9_]+)\s*,\s*(true|false)\s*\})TOK");
    const std::regex token("%[A-Z0-9_]+%");
    for (const auto& name : {std::string("HEAD"), std::string("TAIL")}) {
        const auto html = extract(source, "const char HTML_" + name + "[] PROGMEM = R\"rawliteral(", ")rawliteral\";");
        const auto table =
                extract(source, "const PortalToken k" + std::string(name == "HEAD" ? "Head" : "Tail") + "Tokens[] = {",
                        "};");
        std::map<std::string, std::string> replacements{{"%DEVICE%", "XTEINK X4"}};
        for (std::sregex_iterator it(table.begin(), table.end(), entry), end; it != end; ++it)
            replacements[(*it)[1].str()] = "translated:" + (*it)[2].str();
        auto expected = html;
        size_t tokens = 0;
        for (std::sregex_iterator it(html.begin(), html.end(), token), end; it != end; ++it) {
            assert(replacements.count(it->str()));
            tokens++;
        }
        assert(tokens > 10);
        for (const auto& replacement : replacements) {
            size_t at = 0;
            while ((at = expected.find(replacement.first, at)) != std::string::npos) {
                expected.replace(at, replacement.first.size(), replacement.second);
                at += replacement.second.size();
            }
        }
        assert(stream(html, replacements, largest) == expected);
    }
    puts("Portal templates preserve all translations and JavaScript with a bounded 512-byte output buffer.");
}
