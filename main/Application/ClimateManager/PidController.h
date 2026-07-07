#pragma once
#include <cmath>

// Clean-room reimplementation of the ESPHome climate PID (deadband variant)
// as configured in diyless-thermostat-3.yaml. ESPHome is GPLv3 — the algorithm
// and constants are reused, the source is not. Heat-only: output is a demand
// fraction clamped to [0,1]. kd is 0, so there is no derivative term at all.
//
// error = setpoint - measured (positive = too cold = wants heat). Inside the
// ±deadband the integral gain is cut (anti-windup near target) and the output
// is averaged over a longer window to stop hunting; outside, a shorter window.
class PidController
{
public:
    static constexpr float Kp = 0.77f;
    static constexpr float Ki = 0.0005f;      // per second
    static constexpr float Deadband = 0.5f;   // ±°C around setpoint
    static constexpr float DeadbandKiMul = 0.15f;
    static constexpr int   AvgOutside = 10;
    static constexpr int   AvgInside  = 15;
    static constexpr int   RingSize   = 15;

    float Update(float setpoint, float measured, float dtSeconds)
    {
        float error = setpoint - measured;
        bool  inBand = fabsf(error) <= Deadband;
        float ki = inBand ? Ki * DeadbandKiMul : Ki;

        integral_ += error * dtSeconds;
        // Clamp the integral *term* (ki·integral) to [0,1] so windup cannot
        // accumulate while the boiler saturates; back-solve integral_ to the
        // boundary using the current ki.
        float iTerm = ki * integral_;
        if (iTerm > 1.0f)      { iTerm = 1.0f; integral_ = 1.0f / ki; }
        else if (iTerm < 0.0f) { iTerm = 0.0f; integral_ = 0.0f; }

        float raw = Kp * error + iTerm;   // kd == 0
        if (raw < 0.0f) raw = 0.0f;
        if (raw > 1.0f) raw = 1.0f;

        ring_[head_] = raw;
        head_ = (head_ + 1) % RingSize;
        if (count_ < RingSize) count_++;

        int window = inBand ? AvgInside : AvgOutside;
        if (window > count_) window = count_;
        float sum = 0.0f;
        for (int i = 0; i < window; i++)
            sum += ring_[(head_ - 1 - i + RingSize) % RingSize];
        return sum / window;
    }

    void Reset()
    {
        integral_ = 0.0f;
        head_ = 0;
        count_ = 0;
    }

private:
    float integral_ = 0.0f;
    float ring_[RingSize] = {};
    int   head_ = 0;
    int   count_ = 0;
};
