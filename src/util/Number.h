// hpvr - text to number, the same on every system

#pragma once

#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace hpvr::util {

// True when all of `text` is one finite decimal number ("1", "-0.25", "3.5e2"); no
// spaces, no leading '+', no "inf" / "nan". Always reads a '.' as the decimal point, whatever the system locale.
// Uses std::from_chars where the C++ library has it for double (GCC, MSVC); Apple's
// libc++ in Xcode 16 does not, so there a stream in the "C" locale reads the number.
inline bool ParseDouble(std::string_view text, double& value)
{
    if (text.empty())
        return false;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    double parsed = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
#else
    const char first = text.front();
    if (first == '+' || first == ' ' || first == '\t')   // from_chars accepts neither
        return false;
    std::istringstream stream{std::string(text)};
    stream.imbue(std::locale::classic());
    double parsed = 0.0;
    stream >> parsed;
    if (stream.fail() || !stream.eof())
        return false;
    value = parsed;
    return true;
#endif
}

} // namespace hpvr::util
