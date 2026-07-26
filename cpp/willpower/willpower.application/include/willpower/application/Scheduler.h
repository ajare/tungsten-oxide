#pragma once

#include <windows.h>
#include <vector>

#include "willpower/application/Platform.h"
#include "willpower/application/SchedulerTask.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		class WP_APPLICATION_API Scheduler
		{
			struct CurrentTask
			{
				SchedulerTask* task;
				int cumulativeMilliseconds;
			};

		private:

			LARGE_INTEGER mFrequency;

			int mMicroseconds, mTotalMicrosecondsAllocated;

			std::vector<CurrentTask> mTasks;

			bool mExecuting;

		public:

			explicit Scheduler(int microseconds);

			void setMicrosecondsAllocated(int microseconds);

			int getMicrosecondsAllocated() const;

			void addTask(SchedulerTask* task, int microseconds);

			void execute(float frameTime);
		};

	} // application
} // WP_NAMESPACE
