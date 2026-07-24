#pragma once

#include <string>
#include <chrono> 

#include "willpower/common/Platform.h"


namespace WP_NAMESPACE
{

    class WP_COMMON_API Timer
    {
        std::chrono::high_resolution_clock::time_point mTimeStarted;

        std::chrono::high_resolution_clock::duration mDuration = {};

        bool mPaused;

    public:

        Timer();

        void restart();

        void resume();

        void pause();

        int64_t elapsedNanoseconds();

        static std::string nsToString(int64_t ns);

        std::string elapsedStr();
    };

} // WP_NAMESPACE
