#include "OrientationEstimator.h"

#include <math.h>
#include <Arduino.h>

namespace
{
  static constexpr float kRadToDeg = 180.0f / 3.14159265359f;
  static constexpr float kDegToRad = 3.14159265359f / 180.0f;

  // Tilt-compensated magnetometer reference (yaw from mag in current tilt frame)
  float getTiltCompensatedYaw(const Quaternion &q, float mxT, float myT, float mzT)
  {
    // Extract roll and pitch from quaternion
    float roll, pitch, yaw;
    q.toEulerDegrees(roll, pitch, yaw);

    float rollRad = roll * kDegToRad;
    float pitchRad = pitch * kDegToRad;

    // Tilt compensate magnetometer readings
    float mxComp = mxT * cosf(pitchRad) + mzT * sinf(pitchRad);
    float myComp = mxT * sinf(rollRad) * sinf(pitchRad) + myT * cosf(rollRad) - mzT * sinf(rollRad) * cosf(pitchRad);

    // Get yaw from tilt-compensated components
    float magYaw = atan2f(-myComp, mxComp) * kRadToDeg;
    if (magYaw < 0.0f)
      magYaw += 360.0f;

    return magYaw;
  }

  // Get reference quaternion from accelerometer + magnetometer (absolute reference)
  Quaternion getReferenceQuaternion(float axG, float ayG, float azG, float mxT, float myT, float mzT)
  {
    // Roll and pitch from accelerometer
    float roll = atan2f(ayG, azG) * kRadToDeg;
    float pitch = atan2f(-axG, sqrtf(ayG * ayG + azG * azG)) * kRadToDeg;

    // Yaw from tilt-compensated magnetometer
    float rollRad = roll * kDegToRad;
    float pitchRad = pitch * kDegToRad;
    float mxComp = mxT * cosf(pitchRad) + mzT * sinf(pitchRad);
    float myComp = mxT * sinf(rollRad) * sinf(pitchRad) + myT * cosf(rollRad) - mzT * sinf(rollRad) * cosf(pitchRad);
    float yaw = atan2f(-myComp, mxComp) * kRadToDeg;
    if (yaw < 0.0f)
      yaw += 360.0f;

    // Negate yaw: atan2f(-myComp, mxComp) gives clockwise-positive (compass)
    // but fromEulerDegrees uses counterclockwise-positive (standard math).
    // Passing -yaw creates the correct physical quaternion so gyro and mag agree.
    return Quaternion::fromEulerDegrees(roll, pitch, -yaw);
  }
}

OrientationEstimator::OrientationEstimator(float alpha)
    : _gyroBlendFactor(alpha), _q(), _hasBootstrapSample(false)
{
  _q.w = 1.0f;
  _q.x = 0.0f;
  _q.y = 0.0f;
  _q.z = 0.0f;
}

void OrientationEstimator::reset()
{
  _q.w = 1.0f;
  _q.x = 0.0f;
  _q.y = 0.0f;
  _q.z = 0.0f;
  _hasBootstrapSample = false;
}

Orientation OrientationEstimator::update(int16_t ax, int16_t ay, int16_t az,
                                         int16_t gx, int16_t gy, int16_t gz,
                                         int16_t mx, int16_t my, int16_t mz,
                                         float dtSeconds)
{
  // Convert raw values to physical units
  const float axG  = ax * kAccelScale;
  const float ayG  = ay * kAccelScale;
  const float azG  = az * kAccelScale;
  const float gxDs = gx * kGyroScale - kGyroOffsetX;
  const float gyDs = gy * kGyroScale - kGyroOffsetY;
  const float gzDs = gz * kGyroScale - kGyroOffsetZ;
  const float mxT  =  (mx * kMagScale - kMagOffsetX) * kMagScaleX;
  const float myT  =  (my * kMagScale - kMagOffsetY) * kMagScaleY;
  const float mzT  = -(mz * kMagScale - kMagOffsetZ) * kMagScaleZ; // AK8963 Z is inverted relative to accel/gyro Z (datasheet Fig 4 vs Fig 5)

  // Bootstrap: initialize from accelerometer + magnetometer
  if (!_hasBootstrapSample)
  {
    _q = getReferenceQuaternion(axG, ayG, azG, mxT, myT, mzT);
    _hasBootstrapSample = true;

    Orientation result;
    _q.toEulerDegrees(result.roll, result.pitch, result.yaw);
    result.yaw = fmodf(360.0f - result.yaw + 360.0f, 360.0f); // CCW → CW compass
    result.yaw = fmodf(result.yaw - kYawOffset + 360.0f, 360.0f);
    return result;
  }

  // Gyro integration: create rotation quaternion from angular velocity
  float gxRad = gxDs * kDegToRad;
  float gyRad = gyDs * kDegToRad;
  float gzRad = gzDs * kDegToRad;

  float angle = sqrtf(gxRad * gxRad + gyRad * gyRad + gzRad * gzRad) * dtSeconds;
  Quaternion qGyro;
  if (angle > 0.0001f)
  {
    float s = sinf(angle * 0.5f) / angle;
    qGyro.w = cosf(angle * 0.5f);
    qGyro.x = gxRad * dtSeconds * s;
    qGyro.y = gyRad * dtSeconds * s;
    qGyro.z = gzRad * dtSeconds * s;
  }
  else
  {
    qGyro.w = 1.0f;
    qGyro.x = 0.0f;
    qGyro.y = 0.0f;
    qGyro.z = 0.0f;
  }

  // Integrate gyro into current orientation
  Quaternion qGyroIntegrated = Quaternion::multiply(_q, qGyro);
  qGyroIntegrated.normalize();

  // Get reference quaternion from accel + mag
  Quaternion qRef = getReferenceQuaternion(axG, ayG, azG, mxT, myT, mzT);

  // During fast rotation the accelerometer reads centripetal acceleration on top
  // of gravity, making qRef completely unreliable. Skip the blend entirely and
  // use pure gyro — bias is calibrated so short-term drift is negligible.
  const float gyroMagDegS = sqrtf(gxDs * gxDs + gyDs * gyDs + gzDs * gzDs);
  if (gyroMagDegS > kFastRotationThresholdDegS)
  {
    _q = qGyroIntegrated;
  }
  else
  {
    _q = Quaternion::slerp(qRef, qGyroIntegrated, _gyroBlendFactor);
  }
  _q.normalize();

  // Convert to Euler angles for output
  Orientation result;
  _q.toEulerDegrees(result.roll, result.pitch, result.yaw);
  result.yaw = fmodf(360.0f - result.yaw + 360.0f, 360.0f); // CCW → CW compass
  result.yaw = fmodf(result.yaw - kYawOffset + 360.0f, 360.0f);

  return result;
}
