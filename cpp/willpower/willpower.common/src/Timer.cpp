#include <cstdlib>
#include <iomanip>
#include <sstream>

#include "willpower/common/Timer.h"


namespace WP_NAMESPACE
{
    using namespace std;

    Timer::Timer()
        : mPaused(false)
    {
        mTimeStarted = chrono::high_resolution_clock::now();
    }

    void Timer::Timer::restart()
    {
        mPaused = false;
        mDuration = {};
        mTimeStarted = chrono::high_resolution_clock::now();
    }

    void Timer::resume()
    {
        if (!mPaused)
        {
            return;
        }

        mPaused = false;
        mTimeStarted = chrono::high_resolution_clock::now();
    }

    void Timer::pause()
    {
        if (mPaused)
        {
            return;
        }

        chrono::high_resolution_clock::time_point now =
            chrono::high_resolution_clock::now();

        mDuration += (now - mTimeStarted);
        mPaused = true;
    }

    int64_t Timer::elapsedNanoseconds()
    {
        if (!mPaused)
        {
            chrono::high_resolution_clock::time_point now =
                chrono::high_resolution_clock::now();

            mDuration += (now - mTimeStarted);
        }

        return chrono::duration_cast<chrono::nanoseconds>(mDuration).count();
    }

    string Timer::nsToString(int64_t ns)
    {
        int nsecs_log10 = static_cast<int>(log10(ns));

        ostringstream os{};
        os.precision(static_cast<uint8_t>(2.0 - (nsecs_log10 % 3)));

        os << fixed;
        if (nsecs_log10 < 6)
            os << ns * 1.0e-3 << " us";
        else if (nsecs_log10 < 9)
            os << ns * 1.0e-6 << " ms";
        else
            os << ns * 1.0e-9 << " s";

        return os.str();
    }

    string Timer::elapsedStr()
    {
        return nsToString(elapsedNanoseconds());
    }

} // WP_NAMESPACE
