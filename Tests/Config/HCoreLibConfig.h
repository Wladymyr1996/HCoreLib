#pragma once

/**
 * @file HCoreLibConfig.h
 * @brief The test build's tuning header. One override, and it is required.
 *
 * Every other limit is left at the value the library ships with, because that
 * is what an application gets unless it says otherwise - and it is therefore
 * the one worth regression-testing. The tests that care about a limit spell it
 * with the macro rather than with a literal, so raising one here moves those
 * tests rather than breaking them.
 *
 * HCoreLib's CMakeLists finds this file through HCORELIB_CONFIG_DIR, which
 * Tests/CMakeLists.txt points at this directory.
 */

/**
 * HValue's string payload, raised from the library's default of 31.
 *
 * NOT a preference. HAuth stores its password as a SHA-256 in hexadecimal,
 * which is 64 characters, and it stores it THROUGH an HValue - so at 31 the
 * hash is silently truncated on its way into config/auth.cfg. Nothing fails at
 * the time: the full hash is still in RAM, so the password keeps verifying for
 * the rest of that boot. It is the NEXT boot, reading the truncated value back,
 * that rejects the correct password forever.
 *
 * HAuthTest.cpp static_asserts on this, so the coupling cannot be undone here
 * without the build saying why.
 */
#define HVALUE_MAX_STRING_LEN 64
