#pragma once

#include <cstddef>

/**
 * @file HCurve.hpp
 * @brief Reading a value off a piecewise-linear curve.
 *
 * The point and the class share this header: a point has no behaviour, so there
 * is nothing for a .cpp of its own to hold.
 */

/** @brief One knee of a curve. */
struct HCurvePoint {
  float x;
  float y;
};

/**
 * @brief A table of knees, interpolated between and clamped outside. A static facade.
 *
 * Library work rather than each application's, because the SHAPE of the problem
 * is the same everywhere it turns up: a battery's voltage against its charge, a
 * thermistor's resistance against temperature, a light sensor's counts against
 * lux. What differs is the table, and a table is data an application owns.
 *
 * @code
 *   // A pack voltage, in millivolts, against percent remaining.
 *   constexpr HCurvePoint kAlkaline3S[] = {
 *     {2700.0f, 0.0f}, {3300.0f, 7.0f}, ... {4800.0f, 100.0f},
 *   };
 *
 *   const float percent = HCurve::at(kAlkaline3S, 7, 4120.0f);
 * @endcode
 *
 * ## Clamped, not extrapolated
 * A reading past either end returns that end's value rather than a line
 * continued into nonsense. A battery reading 5.2 V is a fresh pack somebody
 * measured optimistically or a divider declared wrongly - it is not 130 %
 * charged, and reporting that would be worse than reporting 100.
 *
 * ## The table must be sorted by x, ascending
 * Checked by isSorted(), which an application should feed a static_assert.
 * Unsorted, this returns an answer rather than an error, and a curve that
 * silently reads off the wrong segment is the kind of bug found months later.
 */
class HCurve {
 public:
  HCurve() = delete;

  /**
   * @brief The y for an x, interpolated between the two knees around it.
   *
   * @param points Sorted ascending by x. Must outlive the call.
   * @param count How many. 0 returns 0; 1 returns that point's y for every x.
   * @return The interpolated y, clamped to the first and last y outside the
   *         table's range.
   */
  static float at(const HCurvePoint* points, size_t count, float x) noexcept;

  /**
   * @brief True when the table is sorted ascending by x, with no repeats.
   *
   * constexpr so an application can `static_assert` its own curve rather than
   * discovering at runtime that two knees are the wrong way round. A repeated x
   * is rejected as well as a descending one: two points at the same x make the
   * segment between them infinitely steep, and which one wins is an accident of
   * how the search happens to be written.
   */
  static constexpr bool isSorted(const HCurvePoint* points, size_t count) noexcept {
    for (size_t i = 1; i < count; ++i) {
      if (!(points[i].x > points[i - 1].x)) {
        return false;
      }
    }
    return true;
  }
};
