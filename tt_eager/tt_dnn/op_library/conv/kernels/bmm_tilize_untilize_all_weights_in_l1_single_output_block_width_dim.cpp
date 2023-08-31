#include <cstdint>

#include "compute_kernel_api/tilize.h"
#include "compute_kernel_api/untilize.h"
#include "compute_kernel_api/tile_move_copy.h"
#include "compute_kernel_api/matmul.h"



inline void tilize_in(
    uint32_t in_cb_id,
    uint32_t in_subblock_h,
    uint32_t in_block_w,
    uint32_t in_num_subblocks,
    uint32_t out_cb_id) {

    tilize_init_short(in_cb_id, in_block_w);
    for (uint32_t in_subblock = 0; in_subblock < in_num_subblocks; ++in_subblock) {
        for (uint32_t h = 0; h < in_subblock_h; ++h) {
            cb_wait_front(in_cb_id, in_block_w);
            cb_reserve_back(out_cb_id, in_block_w);;
            tilize_block(in_cb_id, in_block_w, out_cb_id);
            cb_push_back(out_cb_id, in_block_w);
            cb_pop_front(in_cb_id, in_block_w);
        }
    }
    tilize_uninit();
} // tilize_in()

inline void reblock_and_untilize(
    uint32_t num_out_subblocks_in_col,
    uint32_t out_subblock_num_tiles,
    uint32_t out_subblock_h,
    uint32_t out_subblock_w,
    uint32_t out_block_w,
    uint32_t interm_cb_id,
    uint32_t reblock_cb_id,
    uint32_t out_cb_id) {

    uint32_t num_tiles_in_row_of_subblocks = mulsi3(out_subblock_num_tiles, num_out_subblocks_in_col);
    cb_wait_front(interm_cb_id, num_tiles_in_row_of_subblocks);

    int within_block_index = 0;
    for (uint32_t h = 0; h < out_subblock_h; h++) {
        int block_offset = 0;

        // Reblock
        copy_tile_to_dst_init_short();
        cb_reserve_back(reblock_cb_id, out_block_w);
        for (uint32_t n = 0; n < num_out_subblocks_in_col; n++) {
            for (uint32_t w = 0; w < out_subblock_w; w++) {
                uint32_t tile_index = block_offset + within_block_index + w;
                acquire_dst(tt::DstMode::Half);
                copy_tile(interm_cb_id, tile_index, 0);
                pack_tile(0, reblock_cb_id);
                release_dst(tt::DstMode::Half);
            }
            block_offset += out_subblock_num_tiles;
        }
        cb_push_back(reblock_cb_id, out_block_w);

        // Untilize
        untilize_init_short(reblock_cb_id);
        cb_wait_front(reblock_cb_id, out_block_w);
        cb_reserve_back(out_cb_id, out_block_w);
        untilize_block(reblock_cb_id, out_block_w, out_cb_id);
        cb_pop_front(reblock_cb_id, out_block_w);
        cb_push_back(out_cb_id, out_block_w);
        untilize_uninit(reblock_cb_id);

        within_block_index += out_subblock_w;
    }
    cb_pop_front(interm_cb_id, num_tiles_in_row_of_subblocks);
} // reblock_and_untilize()

inline void pack_matmul_subblock(uint32_t cb_id, uint32_t out_subblock_num_tiles) {
    cb_reserve_back(cb_id, out_subblock_num_tiles);
    for (uint32_t i = 0; i < out_subblock_num_tiles; ++i) {
        pack_tile(i, cb_id);
    }
    cb_push_back(cb_id, out_subblock_num_tiles);
}

namespace NAMESPACE {
void MAIN {

    uint32_t in0_block_w = get_compile_time_arg_val(0); // inner block size in tiles
    uint32_t in0_num_subblocks = get_compile_time_arg_val(1); // outer row block size (in inner row blocks)
    uint32_t in0_block_num_tiles =  get_compile_time_arg_val(2); // out_subblock_h*in0_block_w*in0_num_subblocks;
    uint32_t in0_subblock_num_tiles = get_compile_time_arg_val(3);  // out_subblock_h*in0_block_w
    uint32_t in0_subblock_h = get_compile_time_arg_val(4);
    uint32_t in1_num_subblocks = get_compile_time_arg_val(5); // outer column block size (in inner column blocks)
    uint32_t in1_block_num_tiles = get_compile_time_arg_val(6); //out_subblock_w*in0_block_w* in1_num_subblocks;
    uint32_t in1_per_core_w = get_compile_time_arg_val(7); // out_subblock_w*in1_num_subblocks
    // if these are not defined as volatile, it causes code size for TRISC2 to be too large if num_blocks > 1
    volatile uint32_t in0_num_blocks_h = get_compile_time_arg_val(8);
    volatile uint32_t in0_num_blocks_w = get_compile_time_arg_val(9);
    volatile uint32_t in1_num_blocks_w = get_compile_time_arg_val(10);
    uint32_t out_subblock_h = get_compile_time_arg_val(11); // inner row block size in tiles
    uint32_t out_subblock_w = get_compile_time_arg_val(12); // inner column block size in tiles
    uint32_t out_subblock_num_tiles = get_compile_time_arg_val(13); // out_subblock_h * out_subblock_w;
    bool tilize_in0 = get_compile_time_arg_val(14);
    bool untilize_out = get_compile_time_arg_val(15);

    uint32_t out_block_w = in1_per_core_w;
    bool spill = in0_num_blocks_w > 1;

    // CB indices
    uint32_t in0_cb_id                                = tt::CB::c_in0;
    uint32_t in1_cb_id                                = tt::CB::c_in1;
    uint32_t matmul_partials_cb                       = tt::CB::c_intermed0;
    uint32_t tilized_in0_cb_id                        = tt::CB::c_intermed1;
    uint32_t untilize_mode_final_matmul_partials_cb   = tt::CB::c_intermed2;
    uint32_t untilize_mode_reblock_cb                 = tt::CB::c_intermed3;
    uint32_t out_cb_id                                = tt::CB::c_out0;

    mm_init(in0_cb_id, in1_cb_id, out_cb_id);

    cb_wait_front(in1_cb_id, in1_block_num_tiles * in0_num_blocks_w * in1_num_blocks_w); // wait for all weights, in_num_blocks_w == 1

    for(uint32_t in0_block_h_i = 0; in0_block_h_i < in0_num_blocks_h; ++in0_block_h_i) {
        bool enable_reload = false;
        uint32_t in1_index_inner_dim_h_offset = 0;
        for(uint32_t in0_block_w_i = 0; in0_block_w_i < in0_num_blocks_w; ++in0_block_w_i) { // inner dim of act (w)
            bool last_out = (in0_block_w_i == in0_num_blocks_w - 1);
            if (tilize_in0) {
                tilize_in(in0_cb_id, in0_subblock_h, in0_block_w, in0_num_subblocks, tilized_in0_cb_id);
                mm_init_short();
                cb_wait_front(tilized_in0_cb_id, in0_block_num_tiles);
            } else {
                cb_wait_front(in0_cb_id, in0_block_num_tiles);
            }

            int in0_index_subblock_offset = 0;
            for (uint32_t in0_subblock_i = 0; in0_subblock_i < in0_num_subblocks; ++in0_subblock_i) {
                int in1_index_subblock_offset = 0;
                for (uint32_t in1_subblock_i = 0; in1_subblock_i < in1_num_subblocks; ++in1_subblock_i) {
                    acquire_dst(tt::DstMode::Half);
                    if (enable_reload) {
                        // Reconfigure input
                        copy_tile_to_dst_init_short_with_dt(matmul_partials_cb);
                        cb_wait_front(matmul_partials_cb, out_subblock_num_tiles);
                        for (uint32_t i = 0; i < out_subblock_num_tiles; ++i) {
                            copy_tile(matmul_partials_cb, i, i);
                        }
                        cb_pop_front(matmul_partials_cb, out_subblock_num_tiles);
                        // Reconfigure srcA back
                        mm_init_short_with_dt(matmul_partials_cb);
                    } // enable_reload
                    // Compute output sub-block from in0_subblock x in1_subblock
                    int dst_index = 0;
                    int in0_index_h_offset = 0;
                    for (uint32_t h = 0; h < out_subblock_h; ++h) {
                        for (uint32_t w = 0; w < out_subblock_w; ++w) {
                            int in1_index_inner_dim_subblock_offset = 0;
                            for (uint32_t inner_dim = 0; inner_dim < in0_block_w; ++inner_dim) {
                                matmul_tiles(tilize_in0 ? tilized_in0_cb_id : in0_cb_id,                    // in0_cb
                                                in1_cb_id,                                                     // in1_cb
                                                in0_index_subblock_offset + in0_index_h_offset + inner_dim,    // in0 tile
                                                in1_index_inner_dim_h_offset + in1_index_subblock_offset + in1_index_inner_dim_subblock_offset + w,    // in1 tile
                                                dst_index,                                                     // dst
                                                false);
                                in1_index_inner_dim_subblock_offset += in1_per_core_w;
                            } // for in0_block_w
                            ++dst_index;
                        } // for out_subblock_w
                        in0_index_h_offset += in0_block_w;
                    } // for out_subblock_h
                    pack_matmul_subblock(last_out
                                            ? (untilize_out
                                                ? untilize_mode_final_matmul_partials_cb
                                                : out_cb_id)
                                            : matmul_partials_cb,
                                            out_subblock_num_tiles);
                    release_dst(tt::DstMode::Half);
                    in1_index_subblock_offset += out_subblock_w;
                } // for in1_num_subblocks
                if (last_out && untilize_out) {
                    reblock_and_untilize(
                        in1_num_subblocks,
                        out_subblock_num_tiles,
                        out_subblock_h,
                        out_subblock_w,
                        out_block_w,
                        untilize_mode_final_matmul_partials_cb,
                        untilize_mode_reblock_cb,
                        out_cb_id);
                    mm_init_short();
                } // last_out
                in0_index_subblock_offset += in0_subblock_num_tiles;
            }

            if (spill) enable_reload = true;

            cb_pop_front(tilize_in0 ? tilized_in0_cb_id : in0_cb_id, in0_block_num_tiles);
            in1_index_inner_dim_h_offset += in1_block_num_tiles;
        } // for in0_num_blocks_w
    } // for in0_num_blocks_h
    cb_pop_front(in1_cb_id, in1_block_num_tiles * in0_num_blocks_w * in1_num_blocks_w);
} // MAIN
} // NAMESPACE