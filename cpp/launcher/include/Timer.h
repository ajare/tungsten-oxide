#pragma once

#include <deque>

class Timer
{
	std::deque<float> mFrameTimes;

	float mFrameTimer, mFpsResolution;

public:

	Timer();

	virtual void reset() = 0;

	virtual float getDeltaTime() const = 0;

	virtual float getTotalTime() const = 0;

	void addFrameToCounter(float time);

	float getFPS() const;
};