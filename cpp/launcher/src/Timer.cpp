#include "Timer.h"

Timer::Timer()
	: mFrameTimer(0.0f)
	, mFpsResolution(1.0f)
{
}

void Timer::addFrameToCounter(float time)
{
	mFrameTimer += time;
	mFrameTimes.push_back(time);

	while (mFrameTimer > mFpsResolution)
	{
		mFrameTimer -= mFrameTimes.front();
		mFrameTimes.pop_front();
	}
}

float Timer::getFPS() const
{
	return mFrameTimes.size() / mFrameTimer;
}
