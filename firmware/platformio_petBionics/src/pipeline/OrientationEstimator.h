#pragma once

#include <Arduino.h>

struct Orientation
{
  float roll;  // degrees, rotation around X axis (positive = right side down)
  float pitch; // degrees, rotation around Y axis (positive = nose up)
  float yaw;   // degrees, 0-360 clockwise from magnetic north
};

// Complementary filter that fuses accelerometer, gyroscope and magnetometer
// data from the MPU-9250 into Roll/Pitch/Yaw angles.
//
// Scale assumptions (MPU-9250 power-on defaults):
//   Accel  : ±2 g      -> 16384 LSB/g
//   Gyro   : ±250 °/s  -> 131 LSB/(°/s)
//   Mag    : 16-bit mode (AK8963, CNTL1=0x16) -> 0.15 µT/LSB
class OrientationEstimator
{
public:
  // alpha: weight given to the gyroscope in [0,1].
  // 0.98 means 98% gyro + 2% accel/mag reference per sample.
  explicit OrientationEstimator(float alpha = 0.98f);

  // Reset state (call at the start of each acquisition session).
  void reset();

  // Feed one IMU sample and get updated orientation.
  // dtSeconds: time since the previous call in seconds.
  Orientation update(int16_t ax, int16_t ay, int16_t az,
                     int16_t gx, int16_t gy, int16_t gz,
                     int16_t mx, int16_t my, int16_t mz,
                     float dtSeconds);

private:
  float _alpha;
  float _roll;
  float _pitch;
  float _yaw;
  bool _initialized;

  // MPU-9250 default full-scale conversion factors
  static constexpr float kAccelScale = 1.0f / 16384.0f; // LSB -> g
  static constexpr float kGyroScale  = 1.0f / 131.0f;   // LSB -> °/s
  static constexpr float kMagScale   = 0.15f;            // LSB -> µT (AK8963 16-bit)
};
