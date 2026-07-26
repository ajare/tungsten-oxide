#include <GLFW/glfw3.h>

#include "glfw/TimerGLFW.h"

TimerGLFW::TimerGLFW() :
	mStartTime(0.0f),
	mRunningTime(0.0f)
{
}

void TimerGLFW::reset()
{
	mStartTime = (float)glfwGetTime();
	mRunningTime = 0.0f;
}

float TimerGLFW::getDeltaTime() const
{
	auto newTime = glfwGetTime();
	float frameTime = (float)(newTime - mStartTime);

	mStartTime = newTime;
	mRunningTime += frameTime;

	return frameTime;
}

float TimerGLFW::getTotalTime() const
{
	return mRunningTime;
}
