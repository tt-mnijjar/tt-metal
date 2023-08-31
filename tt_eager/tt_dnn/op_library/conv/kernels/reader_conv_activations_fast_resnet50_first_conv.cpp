#include <stdint.h>
#include "dataflow_api.h"
#include "debug_print.h"

void kernel_main() {
    uint32_t i = 0;
    uint32_t act_addr_dram_base  = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_dram_noc_x = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_dram_noc_y = get_arg_val<uint32_t>(i); i+=1;

    uint32_t conv_act_size_w_ = get_arg_val<uint32_t>(i); i+=1;
    uint32_t conv_act_size_h = get_arg_val<uint32_t>(i); i+=1;
    uint32_t conv_act_size_c = get_arg_val<uint32_t>(i); i+=1;
    uint32_t weight_size_h = get_arg_val<uint32_t>(i); i+=1;
    uint32_t weight_size_w = get_arg_val<uint32_t>(i); i+=1;
    uint32_t stride_h_ = get_arg_val<uint32_t>(i); i+=1;
    uint32_t stride_w_ = get_arg_val<uint32_t>(i); i+=1;
    uint32_t pad_h = get_arg_val<uint32_t>(i); i+=1;
    uint32_t pad_w = get_arg_val<uint32_t>(i); i+=1;
    uint32_t conv_output_size_h = get_arg_val<uint32_t>(i); i+=1;
    uint32_t conv_output_size_w = get_arg_val<uint32_t>(i); i+=1;
    uint32_t num_blocks_act_h = get_arg_val<uint32_t>(i); i+=1;
    uint32_t num_blocks_act_w = get_arg_val<uint32_t>(i); i+=1;
    uint32_t num_blocks_weight_w = get_arg_val<uint32_t>(i); i+=1;
    uint32_t num_groups = get_arg_val<uint32_t>(i); i+=1;

    uint32_t act_matrix_height_unpadded = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_matrix_width_unpadded = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_matrix_height = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_matrix_width = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_matrix_height_ntiles = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_matrix_width_ntiles = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_block_h_datums = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_block_w_datums = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_block_h_ntiles = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_block_w_ntiles = get_arg_val<uint32_t>(i); i+=1;
    uint32_t act_block_num_tiles = get_arg_val<uint32_t>(i); i+=1;
    uint32_t src_dram_act_buffer_size_bytes = get_arg_val<uint32_t>(i); i+=1;
    uint32_t dst_l1_act_buffer_size_bytes = get_arg_val<uint32_t>(i); i+=1;

    constexpr bool act_in_dram = get_compile_time_arg_val(0) == 1;
    constexpr uint32_t stride_h = get_compile_time_arg_val(1);
    constexpr uint32_t stride_w = get_compile_time_arg_val(2);
    constexpr uint32_t conv_act_size_w = get_compile_time_arg_val(3);
    constexpr uint32_t conv_output_w_last_index = get_compile_time_arg_val(4) - 1;
    //constexpr uint32_t act_block_width_padding_bytes = get_compile_time_arg_val(1);

    constexpr uint32_t cb_id_act = 0;
    constexpr uint32_t tile_size_pow2_exponent = 11;
    const DataFormat data_format = get_dataformat(cb_id_act);
    uint32_t channel_stick_size = conv_act_size_c;
    uint32_t channel_stick_size_bytes = channel_stick_size << 1;

    const InterleavedPow2AddrGenFast<act_in_dram> s_act = {
        .bank_base_address = act_addr_dram_base,
        .log_base_2_of_page_size = 5 // TODO: send as a compile-time arg, currently C=16 in FP16_B (so 32 B)
    };

    // Assumptions. Must be true. Validate on host.
    // assert(act_block_w_datums == C * weight_size_w)
    // assert(num_blocks_act_w == weight_size_h)
    // assert(act_block_w_datums % C == 0)
    // assert(act_block_w_datums % 32 == 0)
    // assert(act_block_h_datums % 32 == 0)
    // assert(act_block_h_ntiles == act_block_h_datums/32)
    // assert(act_block_w_ntiles == act_block_w_datums/32)
    // assert(act_block_num_tiles == (act_block_h_datums * act_block_w_datums)/1024)

    uint32_t out_w = 0;
    uint32_t out_w_start = 0;
    uint32_t first_c_id_in_2d_row = 0;
    uint32_t first_c_id_in_2d_row_at_out_w_0 = 0;
    uint32_t first_c_id_in_2d_row_start = 0;
    uint32_t first_c_id_in_2d_row_at_out_w_0_start = 0;
    //DPRINT << "Running new conv reader" << ENDL();
    for(uint32_t nbh = 0; nbh < num_blocks_act_h; nbh++) {
        uint32_t c_id_offset_inter_block_col = 0;
        for (uint32_t nbw = 0; nbw < num_blocks_act_w; nbw++) {
            out_w = out_w_start;
            first_c_id_in_2d_row = first_c_id_in_2d_row_start;
            first_c_id_in_2d_row_at_out_w_0 = first_c_id_in_2d_row_at_out_w_0_start;
            cb_reserve_back(cb_id_act, act_block_num_tiles);
            uint32_t l1_write_addr_act = get_write_ptr(cb_id_act);
            uint32_t l1_addr_offset = 0;
            for(uint32_t bh = 0; bh < act_block_h_datums; bh++) {
                uint32_t c_id_offset_inra_block_col = 0;
                for(uint32_t bw = 0; bw < weight_size_w; bw++) {
                    uint32_t read_size_bytes = channel_stick_size_bytes;

                    // read one channel
                    uint32_t channel_id = first_c_id_in_2d_row + c_id_offset_inter_block_col + c_id_offset_inra_block_col;
                    uint32_t dst_addr = l1_write_addr_act + l1_addr_offset;
                    s_act.noc_async_read_page(channel_id, dst_addr);

                    l1_addr_offset += read_size_bytes;
                    c_id_offset_inra_block_col += 1;
                } // for block width
                if(out_w < conv_output_size_w - 1) {
                    out_w += 1;
                    first_c_id_in_2d_row += 2; // channel id stride in the w dimension
                } else {
                    out_w = 0;
                    first_c_id_in_2d_row_at_out_w_0 += 462; // channel id stride in the h dimension
                    first_c_id_in_2d_row = first_c_id_in_2d_row_at_out_w_0;
                }
            } // for block height
            c_id_offset_inter_block_col += 231;
            noc_async_read_barrier();
            cb_push_back(cb_id_act, act_block_num_tiles);
        } // for num of act blocks in inner width dim
        out_w_start = out_w;
        first_c_id_in_2d_row_start  = first_c_id_in_2d_row;
        first_c_id_in_2d_row_at_out_w_0_start = first_c_id_in_2d_row_at_out_w_0;
    } // for num of act blocks in height dim
}