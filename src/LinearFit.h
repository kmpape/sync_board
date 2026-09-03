#pragma once

// Least-squares fit y = slope*x + intercept over points [from, n).
// Shared by the LED and magnet calibration routines.
inline void linearFit(const float* x, const float* y, int from, int n,
                      float& slope, float& intercept) {
  float sx = 0, sy = 0, sxx = 0, sxy = 0;
  const int m = n - from;
  for (int i = from; i < n; i++) {
    sx += x[i];
    sy += y[i];
    sxx += x[i] * x[i];
    sxy += x[i] * y[i];
  }
  slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
  intercept = (sy - slope * sx) / m;
}
