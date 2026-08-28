#include "cpptools/log.h"

namespace cpptools {

namespace {
    LogSink g_sink;
}

void setLogSink(LogSink sink) {
    g_sink = std::move(sink);
}

void log(Severity severity, const std::string& message) {
    if (!g_sink) {
        return;
    }

    try {
        g_sink(severity, message);
    } catch (...) {
        // Drop it - see log.h's comment on this function.
    }
}

} // namespace cpptools
