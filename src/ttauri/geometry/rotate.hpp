// Copyright Take Vos 2021.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "matrix.hpp"
#include "identity.hpp"

namespace tt {
namespace geo {

template<int D>
class rotate {
public:
    static_assert(D == 2 || D == 3, "Only 2D or 3D rotation-matrices are supported");

    rotate(rotate const &) noexcept = default;
    rotate(rotate &&) noexcept = default;
    rotate &operator=(rotate const &) noexcept = default;
    rotate &operator=(rotate &&) noexcept = default;

    [[nodiscard]] rotate(float angle, vector<3> axis) noexcept requires(D == 3) : _v()
    {
        tt_axiom(axis.is_valid());
        tt_axiom(std::abs(hypot(axis) - 1.0f) < 0.0001f);

        ttlet half_angle = angle * 0.5f;
        ttlet C = std::cos(half_angle);
        ttlet S = std::sin(half_angle);

        _v = static_cast<f32x4>(axis) * S;
        _v.w() = C;
    }

    /** Convert quaternion to matrix.
     *
     */
    [[nodiscard]] constexpr operator matrix<D>() const noexcept
    {
        // Original from https://en.wikipedia.org/wiki/Quaternions_and_spatial_rotation
        //   1 - 2(yy + zz) |     2(xy - zw) |     2(xz + yw)
        //       2(xy + zw) | 1 - 2(xx + zz) |     2(yz - xw)
        //       2(xz - yw) |     2(yz + xw) | 1 - 2(xx + yy)

        // Flipping adds and multiplies:
        //   1 - 2(zz + yy) |     2(xy - zw) |     2(yw + xz)
        //       2(zw + yx) | 1 - 2(xx + zz) |     2(yz - xw)
        //       2(zx - yw) |     2(xw + zy) | 1 - 2(yy + xx)

        // All multiplies.
        ttlet x_mul = _v.xxxx() * _v;
        ttlet y_mul = _v.yyyy() * _v;
        ttlet z_mul = _v.zzzz() * _v;

        auto twos = f32x4(-2.0f, 2.0f, 2.0f, 0.0f);
        auto one = f32x4(1.0f, 0.0f, 0.0f, 0.0f);
        ttlet col0 = one + addsub<0b0011>(z_mul.zwxy(), y_mul.yxwz()) * twos;
        one = one.yxzw();
        twos = twos.yxzw();
        ttlet col1 = one + addsub<0b0110>(x_mul.yxwz(), z_mul.wzyx()) * twos;
        one = one.xzyw();
        twos = twos.xzyw();
        ttlet col2 = one + addsub<0b0101>(y_mul.wzyx(), x_mul.zwxy()) * twos;
        one = one.xywz();
        return matrix<D>{col0, col1, col2, one};
    }

    std::pair<float, vector<3>> angle_and_axis() const noexcept requires(D == 3)
    {
        ttlet rcp_length = rcp_hypot<0b0111>(_v);
        ttlet length = 1.0f / rcp_length;

        return {2.0f * std::atan2(length), vector<3>{_v.xyz0() * rcp_length}};
    }

private:
    /** rotation is stored as a quaternion
     * w + x*i + y*j + z*k
     */
    f32x4 _v;
};

}

using rotate2 = geo::rotate<2>;
using rotate3 = geo::rotate<3>;

}
