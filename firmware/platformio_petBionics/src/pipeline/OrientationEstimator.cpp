#include "OrientationEstimator.h"

#include <math.h>

namespace
{
  static constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);
  static constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;

  // Normalize a yaw angle to [0, 360).
  float normalizeYawDegrees(float yawDegrees)
  {
    while (yawDegrees >= 360.0f)
      yawDegrees -= 360.0f;
    while (yawDegrees < 0.0f)
      yawDegrees += 360.0f;
    return yawDegrees;
  }

  // Shortest signed difference between two angles, result in [-180, 180].
  float shortestAngleDifferenceDegrees(float targetDegrees, float currentDegrees)
  {
    float differenceDegrees = targetDegrees - currentDegrees;
    while (differenceDegrees > 180.0f)
      differenceDegrees -= 360.0f;
    while (differenceDegrees < -180.0f)
      differenceDegrees += 360.0f;
    return differenceDegrees;
  }
} // namespace

OrientationEstimator::OrientationEstimator(float alpha)
    : _gyroBlendFactor(alpha), _rollDeg(0.0f), _pitchDeg(0.0f), _yawDeg(0.0f), _hasBootstrapSample(false) {}

void OrientationEstimator::reset()
{
  _rollDeg = 0.0f;
  _pitchDeg = 0.0f;
  _yawDeg = 0.0f;
  _hasBootstrapSample = false;
}

Orientation OrientationEstimator::update(int16_t ax, int16_t ay, int16_t az,
                                         int16_t gx, int16_t gy, int16_t gz,
                                         int16_t mx, int16_t my, int16_t mz,
                                         float dtSeconds)
{
  // ── 1. Convert raw values to physical units ───────────────────────────────
  const float axG = ax * kAccelScale; // g
  const float ayG = ay * kAccelScale;
  const float azG = az * kAccelScale;
  const float gxDs = gx * kGyroScale; // °/s
  const float gyDs = gy * kGyroScale;
  const float gzDs = gz * kGyroScale;
  const float mxT = mx * kMagScale; // µT
  const float myT = my * kMagScale;
  const float mzT = mz * kMagScale;

  // ── 2. Accel-only roll / pitch (absolute reference, drifts under vibration)
  const float rollAcc = atan2f(ayG, azG) * kRadToDeg;
  const float pitchAcc = atan2f(-axG, sqrtf(ayG * ayG + azG * azG)) * kRadToDeg;

  // ── 3. Bootstrap on the first call ────────────────────────────────────────
  if (!_hasBootstrapSample)
  {
    _rollDeg = rollAcc;
    _pitchDeg = pitchAcc;

    // Tilt-compensated yaw from magnetometer
    const float rollRad = _rollDeg * kDegToRad;
    const float pitchRad = _pitchDeg * kDegToRad;
    const float mxComp = mxT * cosf(pitchRad) + mzT * sinf(pitchRad);
    const float myComp = mxT * sinf(rollRad) * sinf(pitchRad) + myT * cosf(rollRad) - mzT * sinf(rollRad) * cosf(pitchRad);
    _yawDeg = normalizeYawDegrees(atan2f(-myComp, mxComp) * kRadToDeg);

    _hasBootstrapSample = true;
    return {_rollDeg, _pitchDeg, _yawDeg};
  }

  // ── 4. Complementary filter: gyro integration + accel correction ──────────
  _rollDeg = _gyroBlendFactor * (_rollDeg + gxDs * dtSeconds) + (1.0f - _gyroBlendFactor) * rollAcc;
  _pitchDeg = _gyroBlendFactor * (_pitchDeg + gyDs * dtSeconds) + (1.0f - _gyroBlendFactor) * pitchAcc;

  // ── 5. Tilt-compensated yaw using current roll/pitch ──────────────────────
  const float rollRad = _rollDeg * kDegToRad;
  const float pitchRad = _pitchDeg * kDegToRad;
  const float mxComp = mxT * cosf(pitchRad) + mzT * sinf(pitchRad);
  const float myComp = mxT * sinf(rollRad) * sinf(pitchRad) + myT * cosf(rollRad) - mzT * sinf(rollRad) * cosf(pitchRad);
  const float yawMag = atan2f(-myComp, mxComp) * kRadToDeg;

  // Blend gyro-integrated yaw with magnetometer reference.
  // Use shortest-path difference to handle the 0/360 wraparound correctly.
  const float yawGyro = normalizeYawDegrees(_yawDeg + gzDs * dtSeconds);
  _yawDeg = normalizeYawDegrees(yawGyro + (1.0f - _gyroBlendFactor) * shortestAngleDifferenceDegrees(yawMag, yawGyro));

  return {_rollDeg, _pitchDeg, _yawDeg};
}
