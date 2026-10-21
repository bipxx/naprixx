#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define NAPARIXX_SSE2 1
#endif

namespace nx {

template <typename T>
bool contiguous_finite_range(const T*, size_t, T&, T&) { return false; }

#ifdef NAPARIXX_SSE2
// SSE2 is part of the x86-64 baseline. Other architectures retain the scalar
// path. Unaligned loads work for views with a non-aligned starting address.
template <>
inline bool contiguous_finite_range<float>(const float* data, size_t count,
                                            float& minimum, float& maximum) {
    if (count < 32) return false;
    __m128 minv = _mm_set1_ps(std::numeric_limits<float>::infinity());
    __m128 maxv = _mm_set1_ps(-std::numeric_limits<float>::infinity());
    __m128 nanv = _mm_setzero_ps();
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        __m128 value = _mm_loadu_ps(data + i);
        minv = _mm_min_ps(minv, value);
        maxv = _mm_max_ps(maxv, value);
        nanv = _mm_or_ps(nanv, _mm_cmpunord_ps(value, value));
    }
    if (_mm_movemask_ps(nanv)) return false;
    float mins[4], maxs[4];
    _mm_storeu_ps(mins, minv); _mm_storeu_ps(maxs, maxv);
    minimum = *std::min_element(mins, mins + 4);
    maximum = *std::max_element(maxs, maxs + 4);
    for (; i < count; ++i) {
        float value;
        std::memcpy(&value, data + i, sizeof(value));
        if (!std::isfinite(value)) return false;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    return std::isfinite(minimum) && std::isfinite(maximum);
}

template <>
inline bool contiguous_finite_range<double>(const double* data, size_t count,
                                             double& minimum, double& maximum) {
    if (count < 32) return false;
    __m128d minv = _mm_set1_pd(std::numeric_limits<double>::infinity());
    __m128d maxv = _mm_set1_pd(-std::numeric_limits<double>::infinity());
    __m128d nanv = _mm_setzero_pd();
    size_t i = 0;
    for (; i + 2 <= count; i += 2) {
        __m128d value = _mm_loadu_pd(data + i);
        minv = _mm_min_pd(minv, value);
        maxv = _mm_max_pd(maxv, value);
        nanv = _mm_or_pd(nanv, _mm_cmpunord_pd(value, value));
    }
    if (_mm_movemask_pd(nanv)) return false;
    double mins[2], maxs[2];
    _mm_storeu_pd(mins, minv); _mm_storeu_pd(maxs, maxv);
    minimum = std::min(mins[0], mins[1]);
    maximum = std::max(maxs[0], maxs[1]);
    for (; i < count; ++i) {
        double value;
        std::memcpy(&value, data + i, sizeof(value));
        if (!std::isfinite(value)) return false;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    return std::isfinite(minimum) && std::isfinite(maximum);
}
#endif
} // namespace nx
