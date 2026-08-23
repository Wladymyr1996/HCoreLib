#include "HCurve.hpp"

float HCurve::at(const HCurvePoint* points, size_t count, float x) noexcept {
  if (points == nullptr || count == 0) {
    return 0.0f;
  }

  // Clamped rather than extrapolated at both ends - see the header for why a
  // reading past the table is not a reading past the scale.
  if (x <= points[0].x) {
    return points[0].y;
  }
  if (x >= points[count - 1].x) {
    return points[count - 1].y;
  }

  // A linear scan, not a binary search: these tables are a handful of points,
  // and the scan is fewer instructions than the search's setup.
  for (size_t i = 1; i < count; ++i) {
    if (x > points[i].x) {
      continue;
    }

    const float span = points[i].x - points[i - 1].x;
    if (span <= 0.0f) {
      // isSorted() exists to stop this reaching a device. If it does anyway,
      // the lower knee is a defensible answer and a divide by zero is not.
      return points[i - 1].y;
    }

    const float position = (x - points[i - 1].x) / span;
    return points[i - 1].y + position * (points[i].y - points[i - 1].y);
  }

  return points[count - 1].y;
}
