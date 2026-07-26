#pragma once

#include "Timer.h"

class TimerGLFW : public Timer
{
	mutable float mStartTime, mRunningTime;

public:

	TimerGLFW();

	void reset();

	float getDeltaTime() const;

	float getTotalTime() const;
};