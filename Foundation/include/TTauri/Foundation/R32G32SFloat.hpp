// Copyright 2020 Pokitec
// All rights reserved.

#pragma once

#include "TTauri/Foundation/vec.hpp"
#include <immintrin.h>
#include <emmintrin.h>
#include <algorithm>

namespace tt {

class R32G32SFloat {
    // Red, Green, Blue, Alpha in binary32 (native endian).
    std::array<float,2> v;

public:
    tt_force_inline R32G32SFloat() = default;
    tt_force_inline R32G32SFloat(R32G32SFloat const &rhs) noexcept = default;
    tt_force_inline R32G32SFloat(R32G32SFloat &&rhs) noexcept = default;
    tt_force_inline R32G32SFloat &operator=(R32G32SFloat const &rhs) noexcept = default;
    tt_force_inline R32G32SFloat &operator=(R32G32SFloat &&rhs) noexcept = default;

    tt_force_inline R32G32SFloat(vec const &rhs) noexcept {
        alignas(16) std::array<float,4> tmp;
        _mm_storeu_ps(tmp.data(), rhs);
        std::memcpy(v.data(), tmp.data(), sizeof(v));
    }

    tt_force_inline R32G32SFloat &operator=(vec const &rhs) noexcept {
        alignas(16) std::array<float,4> tmp;
        _mm_storeu_ps(tmp.data(), rhs);
        std::memcpy(v.data(), tmp.data(), sizeof(v));
        return *this;
    }

    tt_force_inline operator vec () const noexcept {
        alignas(16) std::array<float,4> tmp;
        std::memcpy(tmp.data(), v.data(), sizeof(v));
        tmp[2] = 0.0;
        tmp[3] = 0.0;
        return _mm_loadu_ps(tmp.data());
    }

    [[nodiscard]] tt_force_inline friend bool operator==(R32G32SFloat const &lhs, R32G32SFloat const &rhs) noexcept {
        return lhs.v == rhs.v;
    }
    [[nodiscard]] tt_force_inline friend bool operator!=(R32G32SFloat const &lhs, R32G32SFloat const &rhs) noexcept {
        return !(lhs == rhs);
    }
};

}
