// Copyright Take Vos 2019.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "required.hpp"
#include <date/tz.h>
#include <chrono>
#include <type_traits>

namespace tt {

/** Timestamp
 */
struct hires_system_clock {
    using rep = int64_t;
    using period = std::nano;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<hires_system_clock>;
    static const bool is_steady = false;

	static time_point now() noexcept;

    static std::chrono::system_clock::time_point to_system_time_point(time_point x) noexcept {
        static_assert(std::chrono::system_clock::period::num == 1, "Precision of system clock must be 1 second or better.");
        static_assert(std::chrono::system_clock::period::den <= 1000000000, "Precision of system clock must be 1ns or worse.");

        constexpr int64_t nano_to_sys_ratio = 1000000000LL / std::chrono::system_clock::period::den;

        return std::chrono::system_clock::time_point{
            std::chrono::system_clock::duration(x.time_since_epoch().count() / nano_to_sys_ratio)
        };
    }
};



}
