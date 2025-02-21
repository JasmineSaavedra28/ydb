#pragma once
#include <contrib/libs/apache/arrow/cpp/src/arrow/type.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/type_traits.h>
#include "func_common.h"
#include "bit_cast.h"

#include <cmath>
#include <cstdint>
#include <fenv.h>
#include <type_traits>

namespace NKikimr::NArrow {

struct TRound {

    static constexpr const char * Name = "round";

    template <typename TRes, typename TArg>
    static constexpr TRes Call(arrow::compute::KernelContext*, TArg arg, arrow::Status*) {
        return std::round(arg);
    }

};


struct TRoundBankers {

    static constexpr const char * Name = "roundBankers";

    template <typename TRes, typename TArg>
    static constexpr TRes Call(arrow::compute::KernelContext*, TArg arg, arrow::Status*) {
        fesetround(FE_TONEAREST);
        return std::rint(arg);
    }
};

struct TRoundToExp2 {
    static constexpr const char * Name = "roundToExp2";

    template <typename TRes, typename TArg>
    static constexpr std::enable_if_t<std::is_integral_v<TRes> &&
    (sizeof(TRes) <= sizeof(uint32_t)), TRes>
    Call(arrow::compute::KernelContext*, TArg arg, arrow::Status*) {
        static_assert(std::is_same_v<TRes, TArg>, "");
        return arg <= 0 ? 0 : (TRes(1) << (31 - __builtin_clz(arg)));
    }

    template <typename TRes, typename TArg>
    static constexpr std::enable_if_t<std::is_integral_v<TRes> &&
    (sizeof(TRes) == sizeof(uint64_t)), TRes>
    Call(arrow::compute::KernelContext*, TArg arg, arrow::Status*) {
        static_assert(std::is_same_v<TRes, TArg>, "");
        return arg <= 0 ? 0 : (TRes(1) << (63 - __builtin_clzll(arg)));
    }

    template <typename TRes, typename TArg>
    static constexpr EnableIfFloat32<TRes> Call(arrow::compute::KernelContext*, TArg arg, arrow::Status*) {
        static_assert(std::is_same_v<TRes, TArg>, "");
        return bit_cast<TRes>(bit_cast<uint32_t>(arg) & ~((1ULL << 23) - 1));
    }

    template <typename TRes, typename TArg>
    static constexpr EnableIfFloat64<TRes> Call(arrow::compute::KernelContext*, TArg arg, arrow::Status*) {
        static_assert(std::is_same_v<TRes, TArg>, "");
        return bit_cast<TRes>(bit_cast<uint64_t>(arg) & ~((1ULL << 52) - 1));
    }
};

}
