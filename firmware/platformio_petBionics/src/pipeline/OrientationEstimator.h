#pragma once

#include <Arduino.h>
#include "QuaternionMath.h"

struct Orientation
{
  float roll;  // degrees, rotation around X axis (positive = right side down)
  float pitch; // degrees, rotation around Y axis (positive = nose up)
  float yaw;   // degrees, 0-360 clockwise from magnetic north
};

// Complementary filter using quaternions that fuses accelerometer, gyroscope and magnetometer
// data from the MPU-9250 into Roll/Pitch/Yaw angles.
// Quaternions avoid gimbal lock and handle arbitrary 3D rotations robustly.
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
  float _gyroBlendFactor;
  Quaternion _q; // Internal state as quaternion
  bool _hasBootstrapSample;

  // MPU-9250 default full-scale conversion factors
  static constexpr float kAccelScale = 1.0f / 16384.0f; // LSB -> g
  static constexpr float kGyroScale  = 1.0f / 131.0f;   // LSB -> °/s
  static constexpr float kMagScale   = 0.15f;            // LSB -> µT (AK8963 16-bit)

  // --- Gyroscope bias (°/s) — medir com sensor imóvel, qualquer orientação ---
  static constexpr float kGyroOffsetX = 1.0938f;
  static constexpr float kGyroOffsetY = 1.3516f;
  static constexpr float kGyroOffsetZ = 1.9059f;

  // --- Heading offset (graus) — corrige desvio entre eixo X do sensor e "frente" do dispositivo ---
  static constexpr float kYawOffset = 0.0f;

  // --- Adaptive alpha: acima deste limiar de velocidade angular (°/s) o acelerómetro
  //     lê aceleração centrípeta além da gravidade, tornando qRef não fiável.
  //     Usa kFastRotationAlpha (quase só gyro) para evitar yaw ir na direção errada. ---
  static constexpr float kFastRotationThresholdDegS = 15.0f;

  // --- Magnetómetro: hard-iron (µT) e soft-iron (adimensional) ---
  static constexpr float kMagOffsetX = 37.12f;
  static constexpr float kMagOffsetY = 14.78f;
  static constexpr float kMagOffsetZ = -42.45f;
  static constexpr float kMagScaleX  = 0.9979f;
  static constexpr float kMagScaleY  = 0.9896f;
  static constexpr float kMagScaleZ  = 1.0128f;
};
