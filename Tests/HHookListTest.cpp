#include "HCoreLibTest.hpp"

#include <cstring>

#include <HHookList/HHookList.hpp>

/**
 * @file HHookListTest.cpp
 * @brief Order, capacity, and refusing to grow.
 *
 * The order guarantee is the one that matters. A library module owns the
 * SEQUENCE (only HSleep knows what happens just before a nap) while the
 * application owns the ACTIONS, so "in the order they were added" is a promise
 * an application arranges its shutdown around.
 */

namespace {

char gTrace[16];
size_t gTraceLength = 0;

void record(char c) {
  if (gTraceLength + 1 < sizeof(gTrace)) {
    gTrace[gTraceLength++] = c;
    gTrace[gTraceLength] = '\0';
  }
}

void first() { record('1'); }
void second() { record('2'); }
void third() { record('3'); }

void resetTrace() {
  gTraceLength = 0;
  gTrace[0] = '\0';
}

void checkInvokeOrder() {
  HHookList<4> hooks;
  resetTrace();

  CHECK(hooks.size() == 0);
  hooks.invoke();  // an empty list is a no-op, not a fault
  CHECK_TEXT(gTrace, "");

  CHECK(hooks.add(HHook::create<&first>()));
  CHECK(hooks.add(HHook::create<&second>()));
  CHECK(hooks.add(HHook::create<&third>()));
  CHECK(hooks.size() == 3);

  hooks.invoke();
  CHECK_TEXT(gTrace, "123");

  // Invoking again runs them again: a hook list is not a one-shot.
  hooks.invoke();
  CHECK_TEXT(gTrace, "123123");
}

/** @brief The same callback twice is two registrations, not one. */
void checkDuplicatesAreKept() {
  HHookList<4> hooks;
  resetTrace();

  CHECK(hooks.add(HHook::create<&first>()));
  CHECK(hooks.add(HHook::create<&first>()));
  CHECK(hooks.size() == 2);

  hooks.invoke();
  CHECK_TEXT(gTrace, "11");
}

/** @brief A full list says so rather than growing - there is no heap to grow into. */
void checkCapacity() {
  HHookList<2> hooks;
  resetTrace();

  CHECK(hooks.add(HHook::create<&first>()));
  CHECK(hooks.add(HHook::create<&second>()));
  CHECK(!hooks.add(HHook::create<&third>()));
  CHECK(hooks.size() == 2);

  hooks.invoke();
  CHECK_TEXT(gTrace, "12");  // and the rejected one really is not in there
}

/** @brief An unbound delegate is refused, so invoke() can never call nothing. */
void checkInvalidHookRejected() {
  HHookList<2> hooks;

  CHECK(!hooks.add(HHook()));
  CHECK(hooks.size() == 0);
}

void checkClear() {
  HHookList<2> hooks;
  resetTrace();

  CHECK(hooks.add(HHook::create<&first>()));
  hooks.clear();
  CHECK(hooks.size() == 0);

  hooks.invoke();
  CHECK_TEXT(gTrace, "");

  // And the freed slots are usable again.
  CHECK(hooks.add(HHook::create<&second>()));
  hooks.invoke();
  CHECK_TEXT(gTrace, "2");
}

}  // namespace

void runHookListTests() noexcept {
  HCoreLibTest::begin("HHookList");

  checkInvokeOrder();
  checkDuplicatesAreKept();
  checkCapacity();
  checkInvalidHookRejected();
  checkClear();
}
