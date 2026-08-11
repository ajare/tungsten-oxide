#include "willpower/application/Platform.h"

#if WP_PLATFORM == WP_PLATFORM_WINDOWS

#include "willpower/application/SchedulerTask.h"

#include "willpower/common/Exceptions.h"
#include "willpower/common/WillpowerWalker.h"

using namespace std;

namespace WP_NAMESPACE {
namespace application {

SchedulerTask::SchedulerTask()
    : mMicroseconds(-1), mExecuting(false) {
  if (::QueryPerformanceFrequency(&mFrequency) == FALSE) {
    throw Exception("Could not get timer frequency.");
  }
}

void SchedulerTask::setMicrosecondsAllocated(int nanoseconds) {
  if (mExecuting) {
    throw Exception("Cannot set scheduler timeslice when executing.");
  }

  mMicroseconds = nanoseconds;
}

int SchedulerTask::getMicrosecondsAllocated() const {
  return mMicroseconds;
}

int SchedulerTask::getMicrosecondsSpent() const {
  ASSERT_TRACE(mExecuting && "Cannot get microseconds currently spent when not executing task.");

  // Get task end time
  LARGE_INTEGER curTime;
  if (::QueryPerformanceCounter(&curTime) == FALSE) {
    throw Exception("Could not get timer time.");
  }

  double interval = static_cast<double>(curTime.QuadPart - mStartTime.QuadPart) / mFrequency.QuadPart;
  return (int)(interval * 1000000);
}

void SchedulerTask::execute(float frameTime) {
  mExecuting = true;

  // Start timer
  if (::QueryPerformanceCounter(&mStartTime) == FALSE) {
    throw Exception("Could not get timer time.");
  }

  // Execution implementation
  executeImpl(frameTime);

  mExecuting = false;
}

}  // namespace application
}  // namespace WP_NAMESPACE

#else
#error "Willpower SchedulerTask implementation is supported only on Windows."
#endif
