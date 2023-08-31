#include "tt_dnn/op_library/bmm/bmm_op.hpp"

#include "tt_metal/host_api.hpp"
#include "tt_metal/common/constants.hpp"

#ifdef DEBUG
#include "tt_metal/llrt/tt_debug_print_server.hpp"
#include "tt_metal/hostdevcommon/debug_print_common.h"
#endif

namespace tt {
namespace tt_metal {

Tensor bmm_tilize_untilize(const Tensor& a, const Tensor& b, DataType out_dt,
                           uint32_t a_height_nblocks, uint32_t a_width_nblocks, uint32_t b_width_nblocks,
                           uint32_t a_block_height_ntiles, uint32_t a_block_width_ntiles, uint32_t b_block_width_ntiles,
                           uint32_t out_subblock_height_ntiles, uint32_t out_subblock_width_ntiles,
                           bool tilize_in0, bool untilize_out) {
    // NOTE: Currently only single core implementation exists.
    return operation::run(BMMTilizeUntilize {
                            out_dt,
                            a_height_nblocks, a_width_nblocks, b_width_nblocks,
                            a_block_height_ntiles, a_block_width_ntiles, b_block_width_ntiles,
                            out_subblock_height_ntiles, out_subblock_width_ntiles,
                            tilize_in0, untilize_out},
                          {a, b},
                          {}).at(0);
}

void create_cb_bmm_single_core_tilize_untilize(Program &program,
                                               CoreRange core,
                                               DataFormat in0_df,
                                               DataFormat in1_df,
                                               DataFormat out_df,
                                               uint32_t in0_block_w,
                                               uint32_t in0_block_h,
                                               uint32_t in1_block_w,
                                               uint32_t in0_tile_nbytes,
                                               uint32_t in1_tile_nbytes,
                                               uint32_t out_tile_nbytes,
                                               bool tilize_in0 = true,
                                               bool untilize_out = true) {
    // buffer indices
    uint32_t in0_cb                                 = CB::c_in0;
    uint32_t in1_cb                                 = CB::c_in1;
    uint32_t matmul_partials_cb                     = CB::c_intermed0;
    uint32_t tilize_mode_tilized_in0_cb             = CB::c_intermed1;
    uint32_t untilize_mode_final_matmul_partials_cb = CB::c_intermed2;
    uint32_t untilize_mode_reblock_cb               = CB::c_intermed3;
    uint32_t out_cb                                 = CB::c_out0;

    const uint32_t cb0_ntiles = in0_block_h * in0_block_w * 2;  // double buffer
    const uint32_t cb1_ntiles = in0_block_w * in1_block_w * 2;   // double buffer
    const uint32_t out_ntiles = in0_block_h * in1_block_w;

    // in0 (RM/TM)
    auto cb_in0 = CreateCircularBuffers(
        program,
        in0_cb,
        core,
        cb0_ntiles,
        cb0_ntiles * in0_tile_nbytes,
        in0_df
    );
    // in1 (TM)
    auto cb_in1 = CreateCircularBuffers(
        program,
        in1_cb,
        core,
        cb1_ntiles,
        cb1_ntiles * in1_tile_nbytes,
        in1_df
    );

    if (tilize_in0) {
        // in0 (RM -> TM)
        auto cb_src0_tilized = CreateCircularBuffers(
            program,
            tilize_mode_tilized_in0_cb,
            core,
            cb0_ntiles,
            cb0_ntiles * in0_tile_nbytes,
            in0_df
        );
    }

    if (untilize_out) {
        // partial sums
        auto cb_matmul_partials = CreateCircularBuffers(
            program,
            matmul_partials_cb,
            core,
            out_ntiles,
            out_ntiles * out_tile_nbytes,
            out_df
        );
        // final partial sums
        auto cb_final_matmul_partials = CreateCircularBuffers(
            program,
            untilize_mode_final_matmul_partials_cb,
            core,
            out_ntiles,
            out_ntiles * out_tile_nbytes,
            out_df
        );
        // to reorganize output blocks to fill the whole "per core output block width"
        auto cb_reblock = CreateCircularBuffers(
            program,
            untilize_mode_reblock_cb,
            core,
            in1_block_w,                    // a single row of tiles
            in1_block_w * out_tile_nbytes,
            out_df
        );
        // output
        auto cb_output = CreateCircularBuffers(
            program,
            out_cb,
            core,
            out_ntiles,
            out_ntiles * out_tile_nbytes,
            out_df
        );
    } else {
        // partials and output share same memory
        CoreRangeSet cores(std::set<CoreRange>{core});
        auto cb_matmul_partials = CreateCircularBuffers(
            program,
            {matmul_partials_cb, out_cb},
            cores,
            out_ntiles,
            out_ntiles * out_tile_nbytes,
            out_df
        );
    }
}

operation::ProgramWithCallbacks bmm_single_core_tilize_untilize(
                                    const Tensor &in0,       // activations
                                    const Tensor &in1,       // weights
                                    DataType out_dt,
                                    uint32_t in0_height_nblocks,
                                    uint32_t in0_width_nblocks,
                                    uint32_t in1_width_nblocks,
                                    uint32_t in0_block_height_ntiles,
                                    uint32_t in0_block_width_ntiles,
                                    uint32_t in1_block_width_ntiles,
                                    uint32_t out_subblock_height_ntiles,
                                    uint32_t out_subblock_width_ntiles,
                                    bool tilize_in0,
                                    bool untilize_out,
                                    Tensor &out) {

    uint32_t in0_batch = in0.shape()[0];
    uint32_t in0_channel = in0.shape()[1];
    uint32_t in0_height = in0.shape()[2];
    uint32_t in0_width = in0.shape()[3];
    uint32_t in1_batch = in1.shape()[0];
    uint32_t in1_channel = in1.shape()[1];
    uint32_t in1_height = in1.shape()[2];
    uint32_t in1_width = in1.shape()[3];

    // input matrix shape checks
    TT_ASSERT(in0_batch == 1, "Supports only batch = 1");
    TT_ASSERT(in1_batch == in0_batch, "Batch dimension needs to match for two inputs");
    TT_ASSERT(in0_channel == in1_channel, "Channel dimension needs to match for two inputs");
    TT_ASSERT(in0_width == in1_height, "Input matrices should be compatible for multiplication");

    // tile size checks
    TT_ASSERT(in0_height % constants::TILE_HEIGHT == 0, "Input tensor in0 height needs to be divisible by TILE_HEIGHT");
    TT_ASSERT(in1_height % constants::TILE_HEIGHT == 0, "Input tensor in1 height needs to be divisible by TILE_HEIGHT");
    TT_ASSERT(in0_width % constants::TILE_WIDTH == 0, "Input tensor in0 width needs to be divisible by TILE_WIDTH");
    TT_ASSERT(in1_width % constants::TILE_WIDTH == 0, "Input tensor in1 width needs to be divisible by TILE_WIDTH");

    // device compatibility checks
    TT_ASSERT(in0.storage_type() == StorageType::DEVICE and in1.storage_type() == StorageType::DEVICE, "Operands need to be on the device!");
    TT_ASSERT(in0.device() == in1.device(), "Operands need to be on the same device!");
    TT_ASSERT(in0.buffer() != nullptr && in1.buffer() != nullptr, "Operands need to have buffers allocated on the device!");

    // input data type and formats
    const auto in0_dt = in0.dtype();
    const auto in1_dt = in1.dtype();
    const auto in0_df = datatype_to_dataformat_converter(in0_dt);
    const auto in1_df = datatype_to_dataformat_converter(in1_dt);

    // input data format checks
    TT_ASSERT(in0_dt == DataType::BFLOAT16 || (in0_dt == DataType::BFLOAT8_B && !tilize_in0),
              "in0 only supports BFLOAT16 and BFLOAT8_B data types for now");
    TT_ASSERT(in1_dt == DataType::BFLOAT16 || in1_dt == DataType::BFLOAT8_B, "in1 only supports BFLOAT16 and BFLOAT8_B formats for now!");

    // output data format
    const auto out_df = datatype_to_dataformat_converter(out_dt);

    // out dt checks
    TT_ASSERT(!untilize_out || (untilize_out && out_dt == DataType::BFLOAT16));

    // TODO (AS): Certain mixed-prec cases do not currently work. Assert them out.
    if (!(in0_dt == out_dt && in0_dt == in1_dt && in0_dt == DataType::BFLOAT16) && (tilize_in0 || untilize_out)) {
        TT_ASSERT(false, "TODO: Cases to be debugged");
    }

    const auto in0_tile_nbytes = tile_size(in0_df);
    const auto in1_tile_nbytes = tile_size(in1_df);
    const auto out_tile_nbytes = tile_size(out_df);

    Buffer *src0_dram_buffer = in0.buffer();
    Buffer *src1_dram_buffer = in1.buffer();

    TT_ASSERT(src0_dram_buffer->size() % in0_tile_nbytes == 0, "Buffer size of tensor in0 must be multiple of tile size");
    TT_ASSERT(src1_dram_buffer->size() % in1_tile_nbytes == 0, "Buffer size of tensor in1 must be multiple of tile size");

    Device *device = in0.device();
    CoreCoord core = {0, 0};
    CoreCoord debug_core = {1, 1};

    Program program{};

    CoreRange core_range { .start = core, .end = core };

    // start debug server for kernel dprint
    // int hart_mask = DPRINT_HART_NC | DPRINT_HART_BR;
    // tt_start_debug_print_server(device->cluster(), {0}, {debug_core});

    Buffer *dst_dram_buffer = out.buffer();
    TT_ASSERT(dst_dram_buffer != nullptr, "Output buffer should be allocated on device!");

    // Convert tensor dims to tile dims
    uint32_t in0_height_ntiles = in0_height / constants::TILE_HEIGHT;   // == in0_height_nblocks * in0_block_height_ntiles
    uint32_t in0_width_ntiles = in0_width / constants::TILE_WIDTH;      // == in0_width_nblocks * in0_block_width_ntiles
    uint32_t in1_width_ntiles = in1_width / constants::TILE_WIDTH;      // == in1_width_nblocks * in1_block_width_ntiles
    // Ensure the size arguments match the input tensors
    TT_ASSERT(in0_height_ntiles == in0_height_nblocks * in0_block_height_ntiles, "Mismatch in tensor in0 height!");
    TT_ASSERT(in0_width_ntiles == in0_width_nblocks * in0_block_width_ntiles, "Mismatch tensor in0 width!");
    TT_ASSERT(in1_width_ntiles == in1_width_nblocks * in1_block_width_ntiles, "Mismatch tensor in1 width!");

    {   // debug
        log_debug("in0_height_ntiles = {}", in0_height_ntiles);
        log_debug("in0_width_ntiles = {}", in0_width_ntiles);
        log_debug("in1_width_ntiles = {}", in1_width_ntiles);
    }

    // in0
    uint32_t in0_dram_addr = src0_dram_buffer->address();
    // in0 block info
    uint32_t in0_subblock_h = out_subblock_height_ntiles;
    uint32_t in0_num_blocks_w = in0_width_nblocks;
    uint32_t in0_num_blocks_h = in0_height_nblocks;
    uint32_t in0_block_w = in0_width_ntiles / in0_num_blocks_w;
    uint32_t in0_block_h = in0_height_ntiles / in0_num_blocks_h;
    uint32_t in0_block_num_tiles = in0_block_h * in0_block_w;
    uint32_t in0_num_subblocks = in0_block_h / in0_subblock_h;
    uint32_t in0_subblock_num_tiles = in0_subblock_h * in0_block_w;
    TT_ASSERT(in0_block_h % out_subblock_height_ntiles == 0);

    // in1
    uint32_t in1_dram_addr = src1_dram_buffer->address();
    // in1 block info
    uint32_t in1_subblock_w = out_subblock_width_ntiles;
    uint32_t in1_num_blocks_w = in1_width_nblocks;
    uint32_t in1_num_blocks_h = in0_width_nblocks;
    uint32_t in1_block_w = in1_block_width_ntiles;
    uint32_t in1_num_subblocks = in1_block_w / in1_subblock_w;
    uint32_t in1_block_h = in0_block_w;
    uint32_t in1_block_num_tiles = in1_block_w * in1_block_h;
    TT_ASSERT(in1_block_w % out_subblock_width_ntiles == 0);

    // out
    uint32_t out_dram_addr = dst_dram_buffer->address();
    auto out_dram_noc_xy = dst_dram_buffer->noc_coordinates();
    uint32_t out_dram_noc_x = out_dram_noc_xy.x;
    uint32_t out_dram_noc_y = out_dram_noc_xy.y;
    uint32_t out_subblock_ntiles = out_subblock_height_ntiles * out_subblock_width_ntiles;
    TT_ASSERT(out_subblock_ntiles <= 8, "Subblock can have at most 8 tiles to fit computed intermediates in dst[half]");

    {   // debug
        // in0
        log_debug("in0_dram_addr: {}", in0_dram_addr);
        log_debug("in0_height_ntiles: {}", in0_height_ntiles);
        log_debug("in0_width_ntiles: {}", in0_width_ntiles);
        log_debug("in0_subblock_h: {}", in0_subblock_h);
        log_debug("in0_num_blocks_w: {}", in0_num_blocks_w);
        log_debug("in0_num_blocks_h: {}", in0_num_blocks_h);
        log_debug("in0_block_w: {}", in0_block_w);
        log_debug("in0_block_h: {}", in0_block_h);
        log_debug("in0_block_num_tiles: {}", in0_block_num_tiles);
        log_debug("in0_num_subblocks: {}", in0_num_subblocks);
        log_debug("in0_subblock_num_tiles: {}", in0_subblock_num_tiles);
        // in1
        log_debug("in1_dram_addr: {}", in1_dram_addr);
        log_debug("in1_width_ntiles: {}", in1_width_ntiles);
        log_debug("in1_subblock_w: {}", in1_subblock_w);
        log_debug("in1_num_subblocks: {}", in1_num_subblocks);
        log_debug("in1_block_num_tiles: {}", in1_block_num_tiles);
        log_debug("in1_block_w: {}", in1_block_w);
        log_debug("in1_block_h: {}", in1_block_h);
        log_debug("in1_num_blocks_w: {}", in1_num_blocks_w);
        log_debug("in1_num_blocks_h: {}", in1_num_blocks_h);
        // out
        log_debug("out_dram_addr: {}", out_dram_addr);
        log_debug("out_subblock_height_ntiles: {}", out_subblock_height_ntiles);
        log_debug("out_subblock_width_ntiles: {}", out_subblock_width_ntiles);
        log_debug("out_subblock_ntiles: {}", out_subblock_ntiles);
        // extra
        log_debug("out size: {}", dst_dram_buffer->size());
        log_debug("out pagesize: {}", dst_dram_buffer->page_size());
        // data formats
        log_debug("in0_df: {}", in0_df);
        log_debug("in1_df: {}", in1_df);
        log_debug("out_df: {}", out_df);
    }

    create_cb_bmm_single_core_tilize_untilize(
        program,
        core_range,
        in0_df,
        in1_df,
        out_df,
        in0_block_w,
        in0_block_h,
        in1_block_w,
        in0_tile_nbytes,
        in1_tile_nbytes,
        out_tile_nbytes,
        tilize_in0,
        untilize_out);

    // Reader kernel
    std::string reader_kernel;
    std::vector<uint32_t> reader_rt_args;
    if (tilize_in0) {
        // in0 is row major, in1 is tiled
        // NOTE: this only makes sense for non-tile-shared datatypes for in0
        reader_kernel = "tt_metal/kernels/dataflow/reader_bmm_single_core_tilize_untilize.cpp";
        reader_rt_args = {
            // in0
            in0_dram_addr,
            in0_block_h,
            in0_num_blocks_h,
            in0_num_blocks_w,
            in0_block_num_tiles,
            in0_block_h * constants::TILE_HEIGHT,               // in0_block_nrows,
            in0.element_size(),                                         // UNUSED
            in0_width * in0.element_size(),                             // page size (size of an in0 row)
            in0_block_w * constants::TILE_WIDTH * in0.element_size(),   // size of partial row to fit within a block width
            // in1
            in1_dram_addr,
            in1_block_h,
            in1_block_w,
            in1_num_blocks_w,
            in1_block_num_tiles,
            in1_width_ntiles,
            in1_width_ntiles * in1_block_h,
            in1_block_w
        };
    } else {
        // in0 is tiled, in1 is tiled
        reader_kernel = "tt_metal/kernels/dataflow/reader_bmm_single_core.cpp";
        reader_rt_args = {
            // in0
            in0_dram_addr,
            in0_num_blocks_h,
            in0_num_blocks_w,
            1,                      // in0_stride_w
            in0_width_ntiles,       // in0_stride_h
            in0_block_w,            // in0_next_block_stride
            in0_block_w,            // in0_block_w
            in0_block_h,            // in0_block_h
            in0_block_num_tiles,    // in0_block_num_tiles
            // in1
            in1_dram_addr,          // in1_addr
            in1_num_blocks_w,
            0,                      // in1_start_tile_id
            1,                      // in1_stride_w
            in1_width_ntiles,       // in1_stride_h
            in0_block_w * in1_width_ntiles, // in1_next_block_stride UNUSED
            in1_block_w,                    // in1_block_w
            in1_block_h,                    // in1_block_h
            in1_block_num_tiles,            // in1_block_num_tiles
            in0_width_ntiles * in0_block_h, // in0_next_block_stride_h,
            in0_block_w,                    // in0_next_block_stride_w,
            in1_width_ntiles * in1_block_h, // in1_next_block_stride_h,
            in1_block_w                    // in1_next_block_stride_w
        };
    }
    auto reader_id = CreateDataMovementKernel(
        program,                            // program
        reader_kernel,                      // file name
        core_range,                         // core
        tt_metal::DataMovementConfig{.processor = DataMovementProcessor::RISCV_1, .noc = NOC::RISCV_1_default}
    );

    // number of data elements along height of an in0 block
    uint32_t in0_block_h_data = in0_height / in0_num_blocks_h;

    // Writer kernel
    std::string writer_kernel;
    vector<uint32_t> writer_rt_args;
    if (untilize_out) {
        // out is row major
        writer_kernel = "tt_metal/kernels/dataflow/writer_unary_stick_layout_8bank_blocks.cpp";
        writer_rt_args = {
            out_dram_addr,
            in0_block_h_data,
            in1_block_w * constants::TILE_WIDTH * out.element_size(), // block_row_size
            1,                                                  // batch
            in0_num_blocks_h,
            in1_num_blocks_w,
            in1_width * out.element_size(),   // output_row_size
            in1_block_w * constants::TILE_WIDTH * out.element_size(), // last block_row_size (same as block row size)
            in0_height
        };
    } else {
        // out is tiled
        writer_kernel = "tt_metal/kernels/dataflow/writer_bmm_single_core_tiled.cpp";
        writer_rt_args = {
            out_dram_addr,
            0,                                              // UNUSED
            1,                                              // out_stride_w
            in1_width_ntiles,                               // out_stride_h
            out_subblock_width_ntiles,                      // out_next_subblock_stride_w
            out_subblock_height_ntiles * in1_width_ntiles,  // out_next_subblock_stride_h
            out_subblock_width_ntiles,                      // out_subblock_w
            out_subblock_height_ntiles,                     // out_subblock_h
            out_subblock_ntiles,                            // out_subblock_tile_count
            in1_width_ntiles / out_subblock_width_ntiles,   // out_num_subblocks_w
            in0_height_ntiles / out_subblock_height_ntiles // out_num_subblocks_h
        };
    }

    std::vector<uint32_t> writer_compile_time_args = {(uint32_t) (src0_dram_buffer->buffer_type() == tt_metal::BufferType::DRAM ? 1 : 0)};

    auto writer_id = CreateDataMovementKernel(
        program,                        // program
        writer_kernel,                  // file name
        core_range,                     // core
        tt_metal::DataMovementConfig{.processor = DataMovementProcessor::RISCV_0, .noc = NOC::RISCV_0_default, .compile_args = writer_compile_time_args}
    );

    // Compute kernel
    std::string compute_kernel = "tt_metal/kernels/compute/bmm_tilize_untilize.cpp";
    std::vector<uint32_t> compute_comptime_args = {
        in0_block_w,
        in0_num_subblocks,
        in0_block_num_tiles,
        in0_subblock_num_tiles,
        in0_subblock_h,
        in1_num_subblocks,
        in1_block_num_tiles,
        in1_block_w,
        in0_num_blocks_h,
        in0_num_blocks_w,
        in1_num_blocks_w,
        out_subblock_height_ntiles,
        out_subblock_width_ntiles,
        out_subblock_ntiles,
        tilize_in0,
        untilize_out
    };
    auto bmm_compute_id = CreateComputeKernel(
        program,
        compute_kernel,
        core_range,
        tt_metal::ComputeConfig{.compile_args = compute_comptime_args}
    );

    // Reader rt args
    SetRuntimeArgs(program, reader_id, core_range, reader_rt_args);
    // Writer rt args
    SetRuntimeArgs(program, writer_id, core_range, writer_rt_args);

    // Compile and launch
    bool pass = CompileProgram(device, program);

    TT_ASSERT(pass);

    auto override_runtime_args_callback = [kernel_reader_id = reader_id, kernel_writer_id = writer_id](
                                            const Program &program,
                                            const std::vector<Buffer*>& input_buffers,
                                            const std::vector<Buffer*>& output_buffers) {
        auto in0_dram_buffer = input_buffers.at(0);
        auto in1_dram_buffer = input_buffers.at(1);
        auto out_dram_buffer = output_buffers.at(0);
        CoreCoord core = {0, 0};
        {
            auto runtime_args = GetRuntimeArgs(program, kernel_reader_id, core);
            runtime_args[0] = in0_dram_buffer->address();
            runtime_args[9] = in1_dram_buffer->address();
            SetRuntimeArgs(program, kernel_reader_id, core, runtime_args);
        }
        {
            auto runtime_args = GetRuntimeArgs(program, kernel_writer_id, core);
            runtime_args[0] = out_dram_buffer->address();
            SetRuntimeArgs(program, kernel_writer_id, core, runtime_args);
        }
    };

    return {std::move(program), override_runtime_args_callback};
} // bmm_single_core_tilize_untilize()


void BMMTilizeUntilize::validate(const std::vector<Tensor>& inputs) const {
    const auto& in0 = inputs.at(0);
    const auto& in1 = inputs.at(1);
    // TODO: Currently all validation is part of the primary function from create_program. Move them here.
}

std::vector<Shape> BMMTilizeUntilize::compute_output_shapes(const std::vector<Tensor>& inputs) const {
    const auto& in0 = inputs.at(0);
    const auto& in1 = inputs.at(1);

    auto in0_batch = in0.shape()[0];
    auto in0_channel = in0.shape()[1];
    auto in0_height = in0.shape()[2];
    auto in0_width = in0.shape()[3];
    auto in1_batch = in1.shape()[0];
    auto in1_channel = in1.shape()[1];
    auto in1_height = in1.shape()[2];
    auto in1_width = in1.shape()[3];

    const Shape out_shape { in0_batch, in0_channel, in0_height, in1_width };
    return {out_shape};
}

std::vector<Tensor> BMMTilizeUntilize::create_output_tensors(const std::vector<Tensor>& input_tensors) const {
    auto output_layout = this->untilize_out_ ? Layout::ROW_MAJOR : Layout::TILE;
    return operation::generic_create_output_tensors(*this, input_tensors, this->out_dt_, output_layout, operation::DEFAULT_OUTPUT_MEMORY_CONFIG);
}

operation::ProgramWithCallbacks BMMTilizeUntilize::create_program(const std::vector<Tensor>& inputs,
                                                                  std::vector<Tensor>& outputs) const {
    const auto& in0 = inputs.at(0);
    const auto& in1 = inputs.at(1);
    auto& out = outputs.at(0);
    // NOTE: currently only single core version exists
    return bmm_single_core_tilize_untilize(in0, in1, out_dt_,
                                           in0_nblocks_h_, in0_nblocks_w_, in1_nblocks_w_,
                                           in0_block_ntiles_h_, in0_block_ntiles_w_, in1_block_ntiles_w_,
                                           out_subblock_ntiles_h_, out_subblock_ntiles_w_,
                                           tilize_in0_, untilize_out_,
                                           out);
}

stl::reflection::Attributes BMMTilizeUntilize::attributes() const {
    return {
        {"out_dt", this->out_dt_},
        {"in0_nblocks_h", this->in0_nblocks_h_},
        {"in0_nblocks_w", this->in0_nblocks_w_},
        {"in1_nblocks_w", this->in1_nblocks_w_},
        {"in0_block_ntiles_h", this->in0_block_ntiles_h_},
        {"in0_block_ntiles_w", this->in0_block_ntiles_w_},
        {"in1_block_ntiles_w", this->in1_block_ntiles_w_},
        {"out_subblock_ntiles_h", this->out_subblock_ntiles_h_},
        {"out_subblock_ntiles_w", this->out_subblock_ntiles_w_},
        {"tilize_in0", this->tilize_in0_},
        {"untilize_out", this->untilize_out_}
    };
}

}  // namespace tt_metal
}  // namespace tt