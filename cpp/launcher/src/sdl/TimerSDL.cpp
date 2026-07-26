#include <sdl3/SDL.h>

#include "sdl/TimerSDL.h"

TimerSDL::TimerSDL() :
	mStartTime(0.0f),
	mRunningTime(0.0f)
{
}

void TimerSDL::reset()
{
	mStartTime = SDL_GetTicks() / 1000.0f;
	mRunningTime = 0.0f;
}

float TimerSDL::getDeltaTime() const
{
	float newTime = SDL_GetTicks() / 1000.0f;
	float frameTime = newTime - mStartTime;

	mStartTime = newTime;
	mRunningTime += frameTime;

	return frameTime;
}

float TimerSDL::getTotalTime() const
{
	return mRunningTime;
}
