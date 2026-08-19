#include "HSystemUtils.hpp"

#include <HCoreLib.h>
#include <cstdio>

#if IS_MCU

#include <cinttypes>
#include <cstdint>

#include <esp_timer.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace HSystemUtils {

void getLogTime(char* buffer, size_t maxSize) {
  const uint32_t uptimeMs = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  // PRIu32, not "%u": uint32_t is unsigned int on Xtensa but unsigned long on
  // RISC-V (ESP32-C6), and -Werror=format rejects the mismatch.
  snprintf(buffer, maxSize, "%" PRIu32, uptimeMs);
}

void getThreadId(char* buffer, size_t maxSize) {
  const char* taskName = pcTaskGetName(nullptr);
  if (taskName == nullptr) {
    taskName = "Unknown";
  }
  snprintf(buffer, maxSize, "%s", taskName);
}

uint32_t millis() {
  // esp_timer, NOT xTaskGetTickCount(): the FreeRTOS tick is 10 ms at IDF's
  // default 100 Hz, so a tick-based clock could only measure durations to a
  // tenth of what a debounce needs. esp_timer reads a hardware counter in
  // microseconds, which keeps 1 ms accuracy without raising the tick rate -
  // and the tick rate is exactly what a battery-powered node should not raise,
  // since every tick is an interrupt the CPU has to wake up for.
  //
  // It also keeps running through light sleep, so a timer armed before a nap
  // is still correct after it.
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void sleep(uint32_t ms) {
  if (ms == 0) {
    taskYIELD();
    return;
  }

  // AT LEAST ms, which takes two corrections and neither is optional at 100 Hz.
  //
  // pdMS_TO_TICKS truncates, so a 2 ms wait becomes zero ticks - "wait for the
  // chip" turning into "read it immediately". And vTaskDelay(n) guarantees only
  // n-1 WHOLE ticks: the caller is somewhere inside the current tick already, so
  // the first boundary can arrive at once. vTaskDelay(4) on a 10 ms tick can
  // therefore return after 30 ms, not 40.
  //
  // Rounding up and adding one covers both, at a cost of up to one tick of
  // extra sleep. A driver that reads its sensor early gets a number that looks
  // like a measurement and is not one - which is far more expensive than 10 ms.
  const TickType_t ticks = ((ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS) + 1;
  vTaskDelay(ticks);
}

}  // namespace HUtils

#else

#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <thread>

namespace HSystemUtils {

void getLogTime(char* buffer, size_t maxSize) {
  const auto now = std::chrono::system_clock::now();
  const std::time_t nowTimeT = std::chrono::system_clock::to_time_t(now);
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

  std::tm localTm{};
#if defined(_WIN32)
  localtime_s(&localTm, &nowTimeT);
#else
  localtime_r(&nowTimeT, &localTm);
#endif

  snprintf(buffer, maxSize, "%02d:%02d:%02d:%03d",
           localTm.tm_hour, localTm.tm_min, localTm.tm_sec, static_cast<int>(milliseconds.count()));
}

void getThreadId(char* buffer, size_t maxSize) {
  const size_t hashedId = std::hash<std::thread::id>{}(std::this_thread::get_id());
  snprintf(buffer, maxSize, "%04X", static_cast<uint16_t>(hashedId));
}

uint32_t millis() {
  // steady_clock, not system_clock: this must never move backwards because
  // somebody corrected the wall clock or a DST boundary went past.
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void sleep(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace HUtils

#endif
