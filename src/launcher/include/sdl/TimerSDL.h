#include "Timer.h"

class TimerSDL : public Timer
{
	mutable float mStartTime, mRunningTime;

public:

	TimerSDL();

	void reset();

	float getDeltaTime() const;

	float getTotalTime() const;
};