// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

#define REDUCE_OP PoolType::SUM
#define REDUCE_DIM ReduceDim::REDUCE_ROW

#include "compute_kernel_api/bcast.h"
#include "compute_kernel_api/eltwise_binary.h"
#include "compute_kernel_api/eltwise_unary/negative.h"
#include "compute_kernel_api/mask.h"
#include "compute_kernel_api/reduce.h"
#include "compute_kernel_api/softmax.h"
#include "compute_kernel_api/tile_move_copy.h"
#include "compute_kernel_api.h"
#include "tt_eager/tt_dnn/op_library/moreh_softmax_backward/kernels/common_ckernels.hpp"

namespace NAMESPACE {
void MAIN {
    constexpr uint32_t onetile = 1;

    constexpr auto cb_y = tt::CB::c_in0;
    constexpr auto cb_dy = tt::CB::c_in1;
    constexpr auto cb_bcast_scaler = tt::CB::c_in2;
    constexpr auto cb_mask = tt::CB::c_in3;
    constexpr auto cb_dx = tt::CB::c_out0;

    constexpr auto cb_ydy = tt::CB::c_intermed0;  // y * dy
    constexpr auto cb_sum = tt::CB::c_intermed1;
    constexpr auto cb_inter2 = tt::CB::c_intermed2;
    constexpr auto cb_add = tt::CB::c_intermed3;

    binary_op_init_common(cb_y, cb_bcast_scaler);

    uint32_t N = get_compile_time_arg_val(0);
    uint32_t Wt = get_compile_time_arg_val(1);

    for (uint32_t n = 0; n < N; ++n) {

        #ifdef LOG
            // sum(dy)
            for (uint32_t w = 0; w < Wt; ++w) {
                if (w == Wt - 1) {
                    if (w == 0){
                        ACQ();
                        mask_tile_to_cb(cb_dy, cb_mask, cb_add, /*itile=*/0, /*mtile=*/0, /*pop=*/1, /*popm=*/0);
                        REL();
                    } else {
                        constexpr auto cb_inter0 = tt::CB::c_intermed0;
                        ACQ();
                        mask_tile_to_cb(cb_dy, cb_mask, cb_inter0, /*itile=*/0, /*mtile=*/0, /*pop=*/1, /*popm=*/0);
                        REL();

                        ACQ();
                        add_tiles_to_cb(cb_add, cb_inter0, cb_add);
                        REL();
                    }
                } else {
                    if (w == 0) {
                        ACQ();
                        copy_tile_to_cb(cb_dy, cb_add);
                        REL();
                    }
                    else {
                        ACQ();
                        add_tiles_to_cb(cb_add, cb_dy, cb_add);
                        REL();
                    }
                }
            }

            ACQ();
            reduce_tile_to_cb(REDUCE_OP, REDUCE_DIM, cb_add, cb_bcast_scaler, cb_sum, 1, /*pop0=*/1, /*pop1=*/0);
            REL();

            for (uint32_t w = 0; w < Wt; w += onetile) {
                // exp(y)
                constexpr auto cb_exp = tt::CB::c_intermed0;
                ACQ();
                exp_tile_to_cb(cb_y, cb_exp, 0);
                REL();

                // sum * exp(y)
                ACQ();
                mul_tiles_bcast_cols_to_cb(cb_exp, cb_sum, cb_inter2, 0, 0, /*pop0=*/1, /*pop1=*/0);
                REL();

                // dy - sum * exp(y)
                ACQ();
                sub_tiles_to_cb(cb_dy, cb_inter2, cb_dx);
                REL();
            }

            cb_pop_front(cb_sum, onetile);
        #else
            // step 1, compute y * dy
            for (uint32_t w = 0; w < Wt; ++w) {
                ACQ();
                if (w == Wt - 1) {
                    mul_tiles_and_mask_tile_to_cb(
                        cb_y, cb_dy, cb_mask, cb_ydy, 0, 0, 0, /*pop0=*/1, /*pop1=*/1, /*popm=*/0);
                } else {
                    mul_tiles_to_cb(cb_y, cb_dy, cb_ydy);
                }
                REL();

                if (w == 0) {
                    ACQ();
                    copy_tile_to_cb(cb_ydy, cb_add);
                    REL();
                } else {
                    ACQ();
                    add_tiles_to_cb(cb_add, cb_ydy, cb_add);
                    REL();
                }
            }

            // step 2, compute sum(y * dy)
            ACQ();
            reduce_tile_to_cb(REDUCE_OP, REDUCE_DIM, cb_add, cb_bcast_scaler, cb_sum, 1, /*pop0=*/1, /*pop1=*/0);
            REL();

            // step 3, compute final result
            for (uint32_t w = 0; w < Wt; w += onetile) {
                // dy - sum
                ACQ();
                sub_tiles_bcast_cols_to_cb(cb_dy, cb_sum, cb_inter2, 0, 0, /*pop0=*/1, /*pop1=*/0);
                REL();

                ACQ();
                #ifdef SOFTMAX
                    // (dy - sum) * y
                    mul_tiles_to_cb(cb_y, cb_inter2, cb_dx);
                #else
                    // -(dy - sum) * y
                    mul_tiles_and_negative_to_cb(cb_y, cb_inter2, cb_dx);
                #endif
                REL();
            }

            cb_pop_front(cb_sum, onetile);
        #endif
    }
}
}  // namespace NAMESPACE
