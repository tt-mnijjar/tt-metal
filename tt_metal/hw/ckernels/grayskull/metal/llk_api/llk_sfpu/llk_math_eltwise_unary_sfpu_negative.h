// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once


#include "llk_math_eltwise_unary_sfpu_common_includes.h"
#include "llk_math_eltwise_unary_sfpu_init.h"
#include "llk_math_eltwise_unary_sfpu_0_param.h"
#include "ckernel_sfpu_negative.h"

namespace ckernel {

// New LLK SFPU APIs

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_negative_init() {
    llk_math_eltwise_unary_sfpu_init<SfpuType::negative, APPROXIMATE>();
}

template <bool APPROXIMATE, DstSync Dst = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_negative(uint dst_index, int vector_mode = Dim::RC) {
    llk_math_eltwise_unary_sfpu_0_param<APPROXIMATE, Dst>
                            (ckernel::sfpu::calculate_negative<APPROXIMATE>,
                            ckernel::sfpu::calculate_negative<APPROXIMATE>,
                            dst_index, vector_mode);
}

} // namespace ckernel