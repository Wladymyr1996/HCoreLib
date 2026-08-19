#pragma once

/**
 * @file HCoreLib.h
 * @brief The library's root header: configuration hook and platform detection.
 *
 * Every HCoreLib module includes this before anything else. It is the one place
 * that knows which platform the code is being built for, so modules branch on
 * IS_MCU / IS_DESKTOP instead of on toolchain-specific macros, and there is
 * exactly one file to teach about a new platform.
 *
 * The whole project is built with exceptions and RTTI disabled and must not
 * allocate from the heap after start-up: prefer ETL fixed-capacity containers
 * over their std counterparts.
 */

// Application-owned tuning header. HCoreLib does NOT provide it - the
// application does, and points HCoreLib at it with the HCORELIB_CONFIG_DIR
// CMake variable. The include is unconditional on purpose: a build that forgot
// to supply one should fail here, loudly, rather than silently running on
// defaults nobody chose. Every define inside it is optional and falls back to a
// default declared next to whichever module consumes it (see
// HVALUE_MAX_STRING_LEN, HLOG_MAX_BACKENDS).
#include <HCoreLibConfig.h>

// ESP_PLATFORM is defined by the ESP-IDF build system for every component.
#if defined(ESP_PLATFORM)
#define IS_MCU 1
#define IS_DESKTOP 0
#else
#define IS_MCU 0
#define IS_DESKTOP 1
#endif

/**
 * The library's heartbeat, in milliseconds.
 *
 * Everything in HCoreLib that has to notice things over time exposes update()
 * and expects to be called at this cadence - see HITickable. It is a CADENCE,
 * not a measurement: every duration in the library is measured against HTimer,
 * which reads an absolute clock, so a late or skipped tick delays when an event
 * is noticed and never distorts how long something took.
 *
 * 10 ms is the resolution a human interface needs (a button edge is noticed
 * within one tick) at 100 wake-ups a second, which is what a battery-powered
 * node can afford. Lower it for something that has to react faster; the only
 * cost of raising it is latency.
 */
#ifndef HCORELIB_TICK_MS
#define HCORELIB_TICK_MS 10
#endif
