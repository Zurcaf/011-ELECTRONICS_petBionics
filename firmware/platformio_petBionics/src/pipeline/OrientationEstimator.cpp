#include "OrientationEstimator.h"

#include <math.h>

namespace
{
  static constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);
  static constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;

  // Wrap angle to [0, 360).
  float wrapYaw(float deg)
  {
    while (deg >= 360.0f) deg -= 360.0f;
    while (deg <    0.0f) deg += 360.0f;
    return deg;
  }

  // Shortest signed difference between two angles, result in [-180, 180].
  float angleDiff(float target, float current)
  {
    float d = target - current;
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
  }
} // namespace

OrientationEstimator::OrientationEstimator(float alpha)
    : _alpha(alpha), _roll(0.0f), _pitch(0.0f), _yaw(0.0f), _initialized(false) {}

void OrientationEstimator::reset()
{
  _roll        = 0.0f;
  _pitch       = 0.0f;
  _yaw         = 0.0f;
  _initialized = false;
}

Orientation OrientationEstimator::update(int16_t ax, int16_t ay, int16_t az,
                                          int16_t gx, int16_t gy, int16_t gz,
                                          int16_t mx, int16_t my, int16_t mz,
                                          float dtSeconds)
{
  // ── 1. Convert raw values to physical units ───────────────────────────────
  const float axG  = ax * kAccelScale; // g
  const float ayG  = ay * kAccelScale;
  const float azG  = az * kAccelScale;
  const float gxDs = gx * kGyroScale;  // °/s
  const float gyDs = gy * kGyroScale;
  const float gzDs = gz * kGyroScale;
  const float mxT  = mx * kMagScale;   // µT
  const float myT  = my * kMagScale;
  const float mzT  = mz * kMagScale;

  // ── 2. Accel-only roll / pitch (absolute reference, drifts under vibration)
  const float rollAcc  = atan2f(ayG, azG) * kRadToDeg;
  const float pitchAcc = atan2f(-axG, sqrtf(ayG * ayG + azG * azG)) * kRadToDeg;

  // ── 3. Bootstrap on the first call ────────────────────────────────────────
  if (!_initialized)
  {
    _roll  = rollAcc;
    _pitch = pitchAcc;

    // Tilt-compensated yaw from magnetometer
    const float rRad   = _roll  * kDegToRad;
    const float pRad   = _pitch * kDegToRad;
    const float mxComp = mxT * cosf(pRad) + mzT * sinf(pRad);
    const float myComp = mxT * sinf(rRad) * sinf(pRad)
                       + myT * cosf(rRad)
                       - mzT * sinf(rRad) * cosf(pRad);
    _yaw = wrapYaw(atan2f(-myComp, mxComp) * kRadToDeg);

    _initialized = true;
    return {_roll, _pitch, _yaw};
  }

  // ── 4. Complementary filter: gyro integration + accel correction ──────────
  _roll  = _alpha * (_roll  + gxDs * dtSeconds) + (1.0f - _alpha) * rollAcc;
  _pitch = _alpha * (_pitch + gyDs * dtSeconds) + (1.0f - _alpha) * pitchAcc;

  // ── 5. Tilt-compensated yaw using current roll/pitch ──────────────────────
  const float rRad   = _roll  * kDegToRad;
  const float pRad   = _pitch * kDegToRad;
  const float mxComp = mxT * cosf(pRad) + mzT * sinf(pRad);
  const float myComp = mxT * sinf(rRad) * sinf(pRad)
                     + myT * cosf(rRad)
                     - mzT * sinf(rRad) * cosf(pRad);
  const float yawMag = atan2f(-myComp, mxComp) * kRadToDeg;

  // Blend gyro-integrated yaw with magnetometer reference.
  // Use shortest-path difference to handle the 0/360 wraparound correctly.
  const float yawGyro = wrapYaw(_yaw + gzDs * dtSeconds);
  _yaw = wrapYaw(yawGyro + (1.0f - _alpha) * angleDiff(yawMag, yawGyro));

  return {_roll, _pitch, _yaw};
}
