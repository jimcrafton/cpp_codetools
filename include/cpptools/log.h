#pragma once

#include <functional>
#include <string>

#include "cpptools/diagnostic.h"

namespace cpptools {

// A pluggable logging sink - deliberately not tied to any output mechanism (no OutputDebugString,
// no stdout) so this header stays free of any Windows/console dependency. Reuses Severity
// (diagnostic.h) rather than a second severity enum - there's already exactly one "how bad is
// this" concept in this library.
using LogSink = std::function<void(Severity severity, const std::string& message)>;

// Registers the sink cpptools calls when it wants to log something. The default (no sink ever
// registered, or passing an empty std::function) is silence - a consumer that never calls this
// (cppoutline, cpptools_tests, ...) sees no behavior change at all from before this existed.
void setLogSink(LogSink sink);

// Called internally by cpptools code that wants to log something. No-op if no sink is
// registered. A sink that throws never propagates out of here - a misbehaving log sink must
// never break whatever cpptools operation (e.g. a parse in progress) triggered the log call.
void log(Severity severity, const std::string& message);

} // namespace cpptools
