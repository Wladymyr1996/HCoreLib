#pragma once

/**
 * @file etl_profile.h
 * @brief Project-wide configuration of the Embedded Template Library.
 *
 * ETL picks this file up automatically: etl/platform.h looks for a header named
 * "etl_profile.h" on the include path, and the HEtl component puts this
 * directory there. Keeping the configuration here means the etl submodule stays
 * pristine upstream code that can be updated with a plain git pull.
 *
 * Project stance:
 *  - No heap. ETL's fixed-capacity containers are why the library is here in
 *    the first place; they must be used instead of the std equivalents.
 *  - No exceptions (-fno-exceptions). ETL_THROW_EXCEPTIONS is therefore left
 *    undefined, which makes ETL_ASSERT fall back to assert() - a contract
 *    violation (overflowing a container, indexing out of range) traps in a
 *    debug build and is compiled out when NDEBUG is set.
 */

// Selects etl::mutex's FreeRTOS implementation on the target. Everywhere else
// (host tools, unit tests) ETL falls back to the std::mutex flavour.
#if defined(ESP_PLATFORM)
#define ETL_TARGET_OS_FREERTOS
#endif

// Keeps the file/line of a failed ETL_ASSERT in the message. Costs flash for
// the strings, which is worth it while the project is still being brought up.
#define ETL_VERBOSE_ERRORS
