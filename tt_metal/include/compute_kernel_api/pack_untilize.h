// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once


#include "compute_kernel_api/common.h"
#ifdef TRISC_MATH
#include "llk_math_unary_datacopy_api.h"
#endif
#ifdef TRISC_UNPACK
#include "llk_unpack_A_api.h"
#endif

namespace ckernel {

/**
 * Init function for untilize operations, to be used at the beginning of the kernel.
 */
template <uint32_t block_ct_dim = 8>
ALWI void pack_untilize_init(uint32_t icb, uint32_t ocb)
{
    MATH(( llk_math_eltwise_unary_datacopy_init<A2D, BroadcastType::NONE>(false /*transpose of faces*/, false /*transpose within 16x16 face*/, icb) ));
    MATH(( llk_math_pack_sync_init<SyncHalf>() ));

    PACK(( llk_pack_hw_configure_disaggregated<false>(ocb) ));
    PACK(( llk_pack_untilize_init<block_ct_dim>() ));
    PACK(( llk_setup_outputs() ));
    PACK(( llk_pack_dest_init<SyncHalf, true>() ));

    UNPACK(( llk_setup_operands() ));
    UNPACK(( llk_unpack_A_hw_configure_disaggregated(icb) ));
    UNPACK(( llk_unpack_A_init(false, false, icb) )); // init must be after configure
}

/**
 * Perform the untilize operation on a block of tiles. This simply loops over the provided block size.
*/
template <uint32_t block_ct_dim = 8>
ALWI void pack_untilize_block(uint32_t icb, uint32_t block_rt_dim, uint32_t ocb) {
    for (uint32_t r = 0; r < block_rt_dim; ++r) {
        MATH(( llk_math_wait_for_dest_available<SYNC>() ));
        for (uint32_t c = 0; c < block_ct_dim; ++c) {
            UNPACK(( llk_unpack_A(icb, c) ));
            MATH(( llk_math_eltwise_unary_datacopy<A2D, BroadcastType::NONE, SyncHalf>(c) ));
        }
        MATH(( llk_math_dest_section_done<SYNC>() ));

        PACK(( llk_packer_wait_for_math_done() ));
        PACK(( llk_pack_untilize<block_ct_dim>(1 /*num_blocks*/, ocb) ));
        PACK(( llk_pack_dest_section_done<SYNC>() ));
    }
}

/**
 * Uninitialize untilize operation, to allow initializing another operation.
 */
ALWI void pack_untilize_uninit(uint32_t ocb = 16) {
    PACK(( llk_pack_init(ocb) ));
    PACK(( llk_init_packer_dest_offset_registers<SyncHalf, false>() ));
}

template <uint32_t block_ct_dim = 8>
ALWI void pack_untilize_dst_init_short(uint32_t face_r_dim = 16, uint32_t num_faces = 4)
{
    PACK(( llk_pack_untilize_init<block_ct_dim>(face_r_dim, num_faces) ));
    PACK(( llk_init_packer_dest_offset_registers<SyncHalf, true>() ));
}

template <uint32_t block_ct_dim = 8>
ALWI void pack_untilize_dst(uint32_t ocb, uint32_t face_r_dim = 16, uint32_t num_faces = 4) {
    PACK(( llk_pack_untilize<block_ct_dim>(1 /*num_blocks*/, ocb, face_r_dim, num_faces) ));
}

}
