#pragma once

#include <cstddef>
#include <cstdint>

namespace HSystemUtils {

void getLogTime(char* buffer, size_t maxSize);
void getThreadId(char* buffer, size_t maxSize);

/**
 * @brief Milliseconds since boot. The one clock every HCoreLib duration is
 * measured against.
 *
 * Monotonic and never adjusted, so a difference of two readings is always a
 * real elapsed time. It wraps every 49.7 days, which is harmless as long as
 * callers subtract (`now - then`) rather than compare against a sum - see
 * HTimer, which is the only place in the library that needs to care.
 *
 * Task context only on the MCU.
 */
uint32_t millis();

/**
 * @brief Blocks the CALLING task for `ms`, letting everything else run.
 *
 * For code that owns its own task and has nothing to do until a device is
 * ready - a sensor conversion, a chip's reset time. Anything living on the
 * shared update() tick must NOT call this: see HITickable, where blocking stalls
 * every other tickable behind it. Use an HTimer there instead.
 *
 * On the MCU this is vTaskDelay, so the CPU is free (and may sleep) meanwhile,
 * not a spin. Rounds up to whole FreeRTOS ticks.
 */
void sleep(uint32_t ms);

}  // namespace HUtils
