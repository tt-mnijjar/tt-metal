// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

#include "compute_kernel_api/tile_move_copy.h"
#include "compute_kernel_api/matmul.h"

#ifdef FUSE_BIAS
#include "compute_kernel_api/bcast.h"
#endif

#include "compute_kernel_api/eltwise_unary/sfpu_split_includes.h"


namespace NAMESPACE {
void MAIN {

    constexpr uint32_t in0_block_w = get_compile_time_arg_val(0); // inner block size in tiles
    constexpr uint32_t in0_num_subblocks = get_compile_time_arg_val(1); // outer row block size (in inner row blocks)
    constexpr uint32_t in0_block_num_tiles = get_compile_time_arg_val(2); // out_subblock_h*in0_block_w*in0_num_subblocks;
    constexpr uint32_t in0_subblock_num_tiles = get_compile_time_arg_val(3);  // out_subblock_h*in0_block_w
    constexpr uint32_t in1_num_subblocks = get_compile_time_arg_val(4); // outer column block size (in inner column blocks)
    constexpr uint32_t in1_block_num_tiles = get_compile_time_arg_val(5); //out_subblock_w*in0_block_w* in1_num_subblocks;
    constexpr uint32_t in1_per_core_w = get_compile_time_arg_val(6); // out_subblock_w*in1_num_subblocks
    constexpr uint32_t num_blocks = get_compile_time_arg_val(7);  // outer inner dim (in inner dim blocks)
    constexpr uint32_t out_subblock_h = get_compile_time_arg_val(8); // inner row block size in tiles
    constexpr uint32_t out_subblock_w = get_compile_time_arg_val(9); // inner column block size in tiles
    constexpr uint32_t out_subblock_num_tiles = get_compile_time_arg_val(10); // out_subblock_h * out_subblock_w;
    constexpr uint32_t batch = get_compile_time_arg_val(11); // batch dim

    constexpr uint32_t in0_cb_id = tt::CB::c_in0;
    constexpr uint32_t in1_cb_id = tt::CB::c_in1;
    constexpr uint32_t out_cb_id = tt::CB::c_out0;
    constexpr uint32_t mm_partials_cb_id = tt::CB::c_intermed0;

    #ifdef FUSE_BIAS
    constexpr uint32_t bias_cb_id = tt::CB::c_in3;
    constexpr uint32_t mm_out_cb_id = mm_partials_cb_id;
    #else
    constexpr uint32_t mm_out_cb_id = out_cb_id;
    #endif

    #ifdef SFPU_OP_INIT_ACTIVATION
    SFPU_OP_INIT_ACTIVATION
    #endif

    constexpr bool spill = num_blocks > 1;

    mm_init(in0_cb_id, in1_cb_id, out_cb_id);
    for (uint32_t b = 0; b < batch; b++){
        bool enable_reload = false;
        uint32_t out_num_tiles_to_wait = out_subblock_num_tiles;

        #ifdef PACK_RELU
        // for each batch we start we relu disabled so that intermediate results are not relu'd
        if constexpr(batch > 1) {
            PACK(( llk_pack_relu_config(ReluType::NO_RELU) ));
        }
        #endif

        for(uint32_t block = 0; block < num_blocks; block++)
        {
            bool last_out = block == (num_blocks-1);
            // Configure packer once for pack out without Bias
            #if not defined FUSE_BIAS and defined PACK_RELU
            if (last_out) {
                // if last block we pack the final result with relu enabled
                PACK(( llk_pack_relu_config(ReluType::ZERO_RELU) ));
            }
            #endif

            cb_wait_front(in0_cb_id, in0_block_num_tiles);
            cb_wait_front(in1_cb_id, in1_block_num_tiles);
            int in0_index_subblock_offset = 0;
            for (uint32_t in0_subblock = 0; in0_subblock < in0_num_subblocks; in0_subblock++) {
                int in1_index_subblock_offset = 0;
                for (uint32_t in1_subblock = 0; in1_subblock < in1_num_subblocks; in1_subblock++) {

                    if (enable_reload) {
                        // Reconfigure input
                        copy_tile_to_dst_init_short();
                        unpack_reconfig_data_format_srca(in1_cb_id, mm_partials_cb_id);
                        cb_wait_front(mm_partials_cb_id, out_subblock_num_tiles);
                        tile_regs_acquire();
                        for (uint32_t i = 0; i < out_subblock_num_tiles; i++) {
                            copy_tile(mm_partials_cb_id, i, i);
                        }
                        cb_pop_front(mm_partials_cb_id, out_subblock_num_tiles);
                        // Reconfigure srcA back
                        mm_init_short();
                        unpack_reconfig_data_format_srca(mm_partials_cb_id, in1_cb_id);
                    } else {
                        // just acquire
                        tile_regs_acquire();
                    }

                    // Compute output sub-block from in0_subblock x in1_subblock
                    int dst_index = 0;
                    int in0_index_h_offset = 0;
                    for (uint32_t h = 0; h < out_subblock_h; h++) {
                        for (uint32_t w = 0; w < out_subblock_w; w++) {
                            int in1_index_inner_dim_offset = 0;
                            for (uint32_t inner_dim = 0; inner_dim < in0_block_w; inner_dim++) {
                                int in0_index = in0_index_subblock_offset + in0_index_h_offset + inner_dim;
                                int in1_index = in1_index_subblock_offset + in1_index_inner_dim_offset + w;
                                matmul_tiles(in0_cb_id, in1_cb_id, in0_index, in1_index, dst_index, false /* transpose */);
                                in1_index_inner_dim_offset += in1_per_core_w;
                            }
                            dst_index++;
                        }
                        in0_index_h_offset += in0_block_w;
                    }

                    if (last_out) {
                        // If we fuse bias, we will pack out and run bias + optional sfpu in a separate loop
                        #if not defined FUSE_BIAS and defined SFPU_OP_INIT_ACTIVATION
                        for (uint32_t i = 0; i < out_subblock_num_tiles; i++) {
                            SFPU_OP_FUNC_ACTIVATION
                        }
                        #endif
                        tile_regs_commit();
                        // Pack out to output buffer
                        cb_reserve_back(mm_out_cb_id, out_subblock_num_tiles);
                        tile_regs_wait();
                        for (uint32_t i = 0; i < out_subblock_num_tiles; i++) {
                            pack_tile(i, mm_out_cb_id);
                        }
                        tile_regs_release();
                        cb_push_back(mm_out_cb_id, out_subblock_num_tiles);
                    } else {
                        tile_regs_commit();
                        // Wait for tiles in output buffer to be written out since interm and output share memory
                        if (block == 0) {
                            cb_reserve_back(out_cb_id, out_num_tiles_to_wait);
                            out_num_tiles_to_wait += out_subblock_num_tiles;
                        }
                        // Move partial result to interm buffer
                        cb_reserve_back(mm_partials_cb_id, out_subblock_num_tiles);
                        tile_regs_wait();
                        for (uint32_t i = 0; i < out_subblock_num_tiles; i++) {
                            pack_tile(i, mm_partials_cb_id);
                        }
                        tile_regs_release();
                        cb_push_back(mm_partials_cb_id, out_subblock_num_tiles);
                    }

                    in1_index_subblock_offset += out_subblock_w;
                }
                in0_index_subblock_offset += in0_subblock_num_tiles;
            }

            if constexpr (spill) enable_reload = true;

            cb_pop_front(in0_cb_id, in0_block_num_tiles);
            cb_pop_front(in1_cb_id, in1_block_num_tiles);

        }
        #ifdef FUSE_BIAS
        #ifdef PACK_RELU
        // if last block we pack the final result with relu enabled
        PACK(( llk_pack_relu_config(ReluType::ZERO_RELU) ));
        #endif
        add_bcast_rows_init_short();
        // reconfigure unpacker df for src B
        unpack_reconfig_data_format(in1_cb_id, mm_partials_cb_id, in0_cb_id, bias_cb_id);
        cb_wait_front(bias_cb_id, in1_per_core_w);
        for (uint32_t in0_subblock = 0; in0_subblock < in0_num_subblocks; in0_subblock++) {
            int in1_index_subblock_offset = 0;
            for (uint32_t in1_subblock = 0; in1_subblock < in1_num_subblocks; in1_subblock++) {
                // Redundant wait since we know data was just pushed
                cb_wait_front(mm_partials_cb_id, out_subblock_num_tiles);
                tile_regs_acquire();
                for (uint32_t i = 0, j = 0; j < out_subblock_h; j++) {
                    uint32_t bcast_tile_idx = in1_index_subblock_offset;
                    for (uint32_t k = 0; k < out_subblock_w; k++, i++) {
                        add_tiles_bcast_rows(mm_partials_cb_id, bias_cb_id, i, bcast_tile_idx, i);
                        bcast_tile_idx++;
                    }
                }
                // if there's no SFPU fusion, we commit the regs so packer can start packing
                #ifndef SFPU_OP_INIT_ACTIVATION
                tile_regs_commit();
                #endif

                cb_pop_front(mm_partials_cb_id, out_subblock_num_tiles);


                // sfpu activation
                #ifdef SFPU_OP_INIT_ACTIVATION
                for (uint32_t i = 0; i < out_subblock_num_tiles; i++) {
                    SFPU_OP_FUNC_ACTIVATION
                }
                tile_regs_commit();
                #endif

                // Pack out to output buffer
                cb_reserve_back(out_cb_id, out_subblock_num_tiles);
                tile_regs_wait();
                for (uint32_t i = 0; i < out_subblock_num_tiles; i++) {
                    pack_tile(i, out_cb_id);
                }
                tile_regs_release();
                cb_push_back(out_cb_id, out_subblock_num_tiles);

                in1_index_subblock_offset += out_subblock_w;
            }
        }
        if constexpr(batch > 1) {
            // reconfigure init for matmul
            mm_init_short();
            // reconfigure unpacker df for src B
            unpack_reconfig_data_format(mm_partials_cb_id, in1_cb_id, bias_cb_id, in0_cb_id);
        }
        #endif
    }
}
}