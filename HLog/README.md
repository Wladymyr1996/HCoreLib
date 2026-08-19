# HLog

Logging front end: one call site, a level threshold, and any number of sinks.

```cpp
#define HLOG_MODULE_NAME "App"   // before the include, or lines say "UNKNOWN"
#include <HLog/HLog.hpp>

HInfo("started, %d sensors", count);
HWarning("i2c retry %u", attempt);
```

`HDebug` / `HInfo` / `HWarning` / `HCritical` compare against `HLog::getLevel()`
before doing anything else, so a filtered-out line costs one comparison and does
not even evaluate its arguments. The threshold is global:
`HLog::setLevel(HLogLevel::DEBUG)`.

## Backends

A record is formatted once by `HLog` and then handed to every enabled backend.
Backend 0 is `HConsoleLogBackend`, registered by `HLog` itself: printf with ANSI
colors, which is the IDF monitor on the target and the terminal on desktop.

Anything else is application-owned. Implement `HILogBackend`, give the instance
static storage duration, and register it:

```cpp
class HFileLogBackend : public HILogBackend {
 public:
  void write(const HLogRecord& record) noexcept override;  // record is only valid for this call
};

static HFileLogBackend fileBackend;
const int fileIndex = HLog::addBackend(fileBackend);
```

Control is by index:

| Call | Effect |
| --- | --- |
| `HLog::addBackend(b)` | Registers and enables `b`; returns its index, or `HLog::INVALID_BACKEND_INDEX` when full |
| `HLog::setBackendEnabled(i, false)` | Silences one backend, keeping it registered |
| `HLog::isBackendEnabled(i)` | False for a disabled backend and for an unknown index |
| `HLog::backendCount()` | Number registered, at least 1 |

Silencing the console is `HLog::setBackendEnabled(HLog::CONSOLE_BACKEND_INDEX, false)`.

## Constraints

No allocation: the registry is an `etl::vector` of `HLOG_MAX_BACKENDS` (4) slots
and the message is formatted into a `HLOG_MESSAGE_MAX_LENGTH` (256) byte stack
buffer, longer messages being truncated. Both are overridable per project with
`target_compile_definitions()`.

`write()` runs under HLog's lock, so backends need no locking of their own, must
not call back into `HLog` - the lock is not recursive - and should return
quickly, because every log call pays for every enabled backend.
