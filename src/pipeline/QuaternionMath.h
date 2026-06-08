#pragma once

#include <math.h>

struct Quaternion
{
  float w, x, y, z;

  Quaternion() : w(1.0f), x(0.0f), y(0.0f), z(0.0f) {}
  Quaternion(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}

  // Normalize quaternion to unit length
  void normalize()
  {
    float mag = sqrtf(w * w + x * x + y * y + z * z);
    if (mag > 0.0f)
    {
      w /= mag;
      x /= mag;
      y /= mag;
      z /= mag;
    }
  }

  // Multiply two quaternions (q1 * q2)
  static Quaternion multiply(const Quaternion &q1, const Quaternion &q2)
  {
    return Quaternion(
        q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z,
        q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
        q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x,
        q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w);
  }

  // Conjugate (inverse for unit quaternion)
  Quaternion conjugate() const
  {
    return Quaternion(w, -x, -y, -z);
  }

  // Create rotation quaternion from axis-angle (axis should be normalized)
  static Quaternion fromAxisAngle(float axisX, float axisY, float axisZ, float angleDegrees)
  {
    float angleRad = angleDegrees * (3.14159265359f / 180.0f);
    float halfAngle = angleRad * 0.5f;
    float sinHalf = sinf(halfAngle);
    return Quaternion(cosf(halfAngle), axisX * sinHalf, axisY * sinHalf, axisZ * sinHalf);
  }

  // Convert quaternion to Euler angles (roll, pitch, yaw in degrees)
  void toEulerDegrees(float &roll, float &pitch, float &yaw) const
  {
    const float kRadToDeg = 180.0f / 3.14159265359f;

    // Roll (rotation around X axis)
    float sinr_cosp = 2 * (w * x + y * z);
    float cosr_cosp = 1 - 2 * (x * x + y * y);
    roll = atan2f(sinr_cosp, cosr_cosp) * kRadToDeg;

    // Pitch (rotation around Y axis)
    float sinp = 2 * (w * y - z * x);
    if (fabsf(sinp) >= 1.0f)
      pitch = copysignf(90.0f, sinp);
    else
      pitch = asinf(sinp) * kRadToDeg;

    // Yaw (rotation around Z axis)
    float siny_cosp = 2 * (w * z + x * y);
    float cosy_cosp = 1 - 2 * (y * y + z * z);
    yaw = atan2f(siny_cosp, cosy_cosp) * kRadToDeg;

    // Normalize yaw to [0, 360)
    if (yaw < 0.0f)
      yaw += 360.0f;
  }

  // Create quaternion from Euler angles (roll, pitch, yaw in degrees)
  static Quaternion fromEulerDegrees(float roll, float pitch, float yaw)
  {
    const float kDegToRad = 3.14159265359f / 180.0f;
    float r = roll * kDegToRad * 0.5f;
    float p = pitch * kDegToRad * 0.5f;
    float y = yaw * kDegToRad * 0.5f;

    float cy = cosf(y);
    float sy = sinf(y);
    float cp = cosf(p);
    float sp = sinf(p);
    float cr = cosf(r);
    float sr = sinf(r);

    Quaternion q;
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = sr * cp * cy - cr * sp * sy;
    q.y = cr * sp * cy + sr * cp * sy;
    q.z = cr * cp * sy - sr * sp * cy;
    return q;
  }

  // SLERP (Spherical Linear Interpolation) between two quaternions
  static Quaternion slerp(const Quaternion &q1, const Quaternion &q2, float t)
  {
    Quaternion qa = q1;
    float dotProduct = q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z;

    if (dotProduct < 0.0f)
    {
      qa.w = -qa.w;
      qa.x = -qa.x;
      qa.y = -qa.y;
      qa.z = -qa.z;
      dotProduct = -dotProduct;
    }

    dotProduct = fmaxf(-1.0f, fminf(1.0f, dotProduct));
    float theta = acosf(dotProduct);
    float sinTheta = sinf(theta);

    if (sinTheta < 0.001f)
    {
      return Quaternion(
          qa.w + (q2.w - qa.w) * t,
          qa.x + (q2.x - qa.x) * t,
          qa.y + (q2.y - qa.y) * t,
          qa.z + (q2.z - qa.z) * t);
    }

    float w0 = sinf((1 - t) * theta) / sinTheta;
    float w1 = sinf(t * theta) / sinTheta;

    return Quaternion(
        qa.w * w0 + q2.w * w1,
        qa.x * w0 + q2.x * w1,
        qa.y * w0 + q2.y * w1,
        qa.z * w0 + q2.z * w1);
  }
};
