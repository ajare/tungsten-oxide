#include "willpower/common/DateTime.h"

using namespace std;

namespace WP_NAMESPACE
{
	
	TimeSpan::TimeSpan(int seconds)
	{
		WP_UNUSED(seconds);
	}

	TimeSpan::TimeSpan(int minutes, int seconds)
	{
		WP_UNUSED(minutes);
		WP_UNUSED(seconds);
	}

	TimeSpan::TimeSpan(int hours, int minutes, int seconds)
	{
		WP_UNUSED(hours);
		WP_UNUSED(minutes);
		WP_UNUSED(seconds);
	}

	DateTime::DateTime(int hours, int minutes, int seconds, int day, Month month, int year)
	{
		WP_UNUSED(hours);

		tm tm;
		
		tm.tm_sec = seconds;
		tm.tm_min = minutes;
		tm.tm_mday = day;
		tm.tm_mon = (int)month;
		tm.tm_year = year - 1900;
		tm.tm_isdst = -1;

		mTimepoint = chrono::system_clock::from_time_t(mktime(&tm));
	}

} // WP_NAMESPACE
