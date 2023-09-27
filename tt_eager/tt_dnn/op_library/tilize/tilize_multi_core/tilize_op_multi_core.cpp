// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <math.h>

#include "tt_dnn/op_library/tilize/tilize_op.hpp"
#include "tt_dnn/op_library/math.hpp"

#include "tt_metal/host_api.hpp"
#include "tt_metal/common/constants.hpp"
#include "tt_metal/detail/util.hpp"
#include "tt_metal/common/math.hpp"

#include "tt_stl/reflection.hpp"

using namespace tt::constants;

namespace tt {

namespace tt_metal {


inline std::tuple<int32_t, int32_t, int32_t, int32_t, CoreRangeSet, CoreRangeSet, CoreRangeSet, uint32_t, uint32_t>
    split_blocks_across_cores(CoreCoord grid_size, uint32_t nblocks) {

    int32_t ncores_x = grid_size.x;
    int32_t ncores_y = grid_size.y;
    int32_t ncores = ncores_x * ncores_y;
    uint32_t nblocks_per_core = nblocks;
    uint32_t nblocks_per_core_cliff = 0;
    int32_t ncores_x_cliff = 0;
    std::set<CoreRange> all_cores;
    std::set<CoreRange> core_range, core_range_cliff;
    if (nblocks <= ncores) {
        nblocks_per_core = 1;
        ncores = nblocks;
        ncores_y = ceil((float) ncores / ncores_x);
        ncores_x_cliff = ncores - (ncores_x * (ncores_y - 1));
        if (ncores_x_cliff == ncores_x) {
            // no cliff, all is perfectly divisible
            ncores_x_cliff = 0;
            core_range.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 1)});
            all_cores.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 1)});
        } else if (ncores_x_cliff == 1) {
            // just one cliff core in the last row
            nblocks_per_core_cliff = 1;
            if (ncores_y > 1) {
                core_range.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                all_cores.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
            }
            core_range_cliff.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(0, ncores_y - 1)});
            all_cores.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(0, ncores_y - 1)});
        } else if (ncores_x_cliff > 1) {
            // both normal and cliff cores in the last row
            nblocks_per_core_cliff = 1;
            if (ncores_y > 1) {
                core_range.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                all_cores.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
            }
            core_range.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 2, ncores_y - 1)});
            core_range_cliff.insert(CoreRange{.start = CoreCoord(ncores_x_cliff - 1, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 1, ncores_y - 1)});
            all_cores.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 1, ncores_y - 1)});
        } else {
            TT_ASSERT(false, "Something went really wrong in splitting blocks across cores {} {}!!", ncores_x, ncores_x_cliff);
        }
    } else {
        nblocks_per_core = ceil((float) nblocks / ncores);
        ncores = ceil((float) nblocks / nblocks_per_core);
        nblocks_per_core_cliff = nblocks - nblocks_per_core * (ncores - 1);
        ncores_y = ceil((float) ncores / ncores_x);
        ncores_x_cliff = ncores - ncores_x * (ncores_y - 1);
        if (nblocks_per_core_cliff == nblocks_per_core) {
            // no special cliff at block level for per core
            if (ncores_x_cliff == ncores_x) {
                // no x_cliff row => all cores are equal
                ncores_x_cliff = 0;
                core_range.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 1)});
                all_cores.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 1)});
            } else if (ncores_x_cliff == 1) {
                // just 1 core as cliff in the last core row
                if (ncores_y > 1) {
                    core_range.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                    all_cores.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                }
                core_range_cliff.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(0, ncores_y - 1)});
                all_cores.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(0, ncores_y - 1)});
            } else if (ncores_x_cliff < ncores_x) {
                // last core row has last core as cliff, rest are normal
                if (ncores_y > 1) {
                    core_range.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                    all_cores.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                }
                core_range.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 2, ncores_y - 1)});
                core_range_cliff.insert(CoreRange{.start = CoreCoord(ncores_x_cliff - 1, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 1, ncores_y - 1)});
                all_cores.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 1, ncores_y - 1)});
            } else {
                TT_ASSERT("Something went really wrong in calculating the core ranges {} {}", ncores_x, ncores_x_cliff);
            }
        } else if (nblocks_per_core_cliff < nblocks_per_core) {
            // last core has unequal blocks
            if (ncores_x_cliff == ncores_x) {
                // ncores x is same throughout
                ncores_x_cliff = 0;
                if (ncores_y > 1) {
                    core_range.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                }
                core_range.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 2, ncores_y - 1)});
                core_range_cliff.insert(CoreRange{.start = CoreCoord(ncores_x_cliff - 1, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 1, ncores_y - 1)});
                all_cores.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 1)});
            } else if (ncores_x_cliff == 1) {
                // last core row only has 1 core, as cliff
                if (ncores_y > 1) {
                    core_range.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                    all_cores.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                }
                core_range_cliff.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(0, ncores_y - 1)});
                all_cores.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(0, ncores_y - 1)});
            } else if (ncores_x_cliff < ncores_x) {
                if (ncores_y > 1) {
                    core_range.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                    all_cores.insert(CoreRange{.start = CoreCoord(0, 0), .end = CoreCoord(ncores_x - 1, ncores_y - 2)});
                }
                core_range.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 2, ncores_y - 1)});
                core_range_cliff.insert(CoreRange{.start = CoreCoord(ncores_x_cliff - 1, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 1, ncores_y - 1)});
                all_cores.insert(CoreRange{.start = CoreCoord(0, ncores_y - 1), .end = CoreCoord(ncores_x_cliff - 1, ncores_y - 1)});
            } else {
                TT_ASSERT(false, "Something went very wrong in calculating core ranges (case 2)");
            }
        } else {
            TT_ASSERT(false, "Somehting went really wrong in splitting blocks across cores (case else)");
        }
    }
    return std::make_tuple(ncores, ncores_x, ncores_x_cliff, ncores_y, all_cores, core_range, core_range_cliff, nblocks_per_core, nblocks_per_core_cliff);
}

operation::ProgramWithCallbacks tilize_multi_core_interleaved(const Tensor &a, Tensor& output) {
    tt_metal::Program program = tt_metal::Program();

    TT_ASSERT(a.dtype() == DataType::BFLOAT16, "Only BFLOAT16 data type supported for tilize.");
    TT_ASSERT(a.layout() == Layout::ROW_MAJOR, "Input is not in RM. This case is not yet supported");

    DataFormat cb_data_format = datatype_to_dataformat_converter(a.dtype());
    uint32_t single_tile_size = detail::TileSize(cb_data_format);

    int32_t ntiles = a.volume() / TILE_HW;
    uint32_t ntiles_per_block = a.shape()[3] / TILE_WIDTH;
    uint32_t nblocks = ceil((float) ntiles / ntiles_per_block);
    uint32_t block_size_nbytes = a.shape()[3] * a.element_size();

    {
        log_debug(LogOp, "ntiles: {}", ntiles);
        log_debug(LogOp, "ntiles_per_block: {}", ntiles_per_block);
        log_debug(LogOp, "nblocks: {}", nblocks);
        log_debug(LogOp, "block_size_nbytes: {}", block_size_nbytes);
    }

    Device *device = a.device();
    auto grid_size = device->compute_with_storage_grid_size();
    auto [ncores, ncores_x, ncores_x_cliff, ncores_y, all_cores, core_range, core_range_cliff, nblocks_per_core, nblocks_per_core_cliff] = split_blocks_across_cores(grid_size, nblocks);

    {
        log_debug(LogOp, "ncores: {}", ncores);
        log_debug(LogOp, "ncores_x: {}", ncores_x);
        log_debug(LogOp, "ncores_x_cliff: {}", ncores_x_cliff);
        log_debug(LogOp, "ncores_y: {}", ncores_y);
        log_debug(LogOp, "nblocks_per_core: {}", nblocks_per_core);
        log_debug(LogOp, "nblocks_per_core_cliff: {}", nblocks_per_core_cliff);
    }

    uint32_t src0_cb_index = CB::c_in0;
    uint32_t num_input_tiles = ntiles_per_block;
    tt_metal::CircularBufferConfig src0_cb_config = tt_metal::CircularBufferConfig(num_input_tiles * single_tile_size, {{src0_cb_index, cb_data_format}})
		.set_page_size(src0_cb_index, single_tile_size);
	auto cb_src0 = tt_metal::CreateCircularBuffer(program, all_cores, src0_cb_config);

    uint32_t output_cb_index = CB::c_out0;
    uint32_t num_output_tiles = ntiles_per_block;
    tt_metal::CircularBufferConfig cb_output_config = tt_metal::CircularBufferConfig(num_output_tiles * single_tile_size, {{output_cb_index, cb_data_format}})
		.set_page_size(output_cb_index, single_tile_size);
    auto cb_output = tt_metal::CreateCircularBuffer(program, all_cores, cb_output_config);

    Buffer *src0_buffer = a.buffer();
    Buffer *dst_buffer = output.buffer();
    TT_ASSERT(dst_buffer != nullptr, "Output buffer should be allocated on device!");

    uint32_t block_width_nbytes = ntiles_per_block * TILE_WIDTH * a.element_size();
    uint32_t num_full_blocks_in_row = 1;
    uint32_t num_leftover_tiles = 0;
    uint32_t leftover_width_in_row = 0;

    /** reader
     */
    bool src0_is_dram = src0_buffer->buffer_type() == BufferType::DRAM ? 1 : 0;
    bool stick_size_is_power_of_two = is_power_of_two_at_least_32(block_size_nbytes);
    uint32_t log2_stick_size = stick_size_is_power_of_two ? (std::uint32_t) std::log2(block_size_nbytes) : 0;
    std::vector<uint32_t> reader_ct_args = {
        (std::uint32_t) src0_is_dram,
        (std::uint32_t) stick_size_is_power_of_two,
        (std::uint32_t) log2_stick_size,
    };
    KernelID unary_reader_kernel_id = CreateDataMovementKernel(
        program,
        "tt_metal/kernels/dataflow/reader_unary_stick_layout_split_rows_interleaved.cpp",
        all_cores,
        DataMovementConfig{
            .processor = DataMovementProcessor::RISCV_1,
            .noc = NOC::RISCV_1_default,
            .compile_args = reader_ct_args});

    /** writer
     */
    bool out_is_dram = dst_buffer->buffer_type() == tt_metal::BufferType::DRAM ? 1 : 0;
    std::vector<uint32_t> writer_ct_args = {
        (std::uint32_t) output_cb_index,
        (std::uint32_t) out_is_dram
    };
    KernelID unary_writer_kernel_id = CreateDataMovementKernel(
        program,
        "tt_metal/kernels/dataflow/writer_unary_interleaved_start_id.cpp",
        all_cores,
        DataMovementConfig{
            .processor = DataMovementProcessor::RISCV_0,
            .noc = NOC::RISCV_0_default,
            .compile_args = writer_ct_args});

    /** compute
     */
    vector<uint32_t> compute_args = {
        nblocks_per_core,
        ntiles_per_block
    };
    vector<uint32_t> compute_args_cliff = {
        nblocks_per_core_cliff,
        ntiles_per_block
    };

    if (core_range.ranges().size() > 0) {
        auto tilize_kernel_id = CreateComputeKernel(
            program,
            "tt_metal/kernels/compute/tilize.cpp",
            core_range,
            ComputeConfig{
                .compile_args = compute_args});
    }
    if (core_range_cliff.ranges().size() > 0) {
        auto tilize_cliff_kernel_id = CreateComputeKernel(
            program,
            "tt_metal/kernels/compute/tilize.cpp",
            core_range_cliff,
            ComputeConfig{
                .compile_args = compute_args_cliff});

    }

    // 1D distribution of blocks across cores
    uint32_t ncores_full = ncores;
    if (nblocks_per_core_cliff > 0 && nblocks_per_core_cliff < nblocks_per_core) {
        // unequal case with cliff on the last core
        ncores_full -= 1;
    }
    uint32_t tile_start_id = 0;
    uint32_t row_start_id = 0;
    for (uint32_t i = 0; i < ncores_full; ++ i) {
        CoreCoord core = {i % ncores_x, i / ncores_x};

        // reader runtime args
        vector<uint32_t> reader_rt_args = {
            src0_buffer->address(),
            nblocks_per_core * TILE_HEIGHT,
            block_size_nbytes,
            ntiles_per_block,
            block_size_nbytes,
            1,              // full blocks in row
            0,              // num leftover tiles
            0,              // leftover width in row
            row_start_id
        };
        // log_debug("reader: {},{} = {} {}", core.x, core.y, block_size_nbytes, row_start_id);

        // writer runtime args
        vector<uint32_t> writer_rt_args = {
            dst_buffer->address(),
            ntiles_per_block * nblocks_per_core,    // ntiles per core
            tile_start_id                           // start id
        };
        // log_debug("writer: {},{} = {} ({})", core.x, core.y, tile_start_id, ntiles_per_block * nblocks_per_core_cliff);

        SetRuntimeArgs(
            program,
            unary_reader_kernel_id,
            core,
            reader_rt_args
        );

        SetRuntimeArgs(
            program,
            unary_writer_kernel_id,
            core,
            writer_rt_args
        );

        tile_start_id += ntiles_per_block * nblocks_per_core;
        row_start_id += TILE_HEIGHT * nblocks_per_core;
    }
    if (ncores_full < ncores) {
        // the last core is a cliff core with nblocks_per_core_cliff blocks
        CoreCoord core = {ncores_full % ncores_x, ncores_full / ncores_x};

        // reader runtime args
        vector<uint32_t> reader_rt_args = {
            src0_buffer->address(),
            nblocks_per_core_cliff * TILE_HEIGHT,
            block_size_nbytes,
            ntiles_per_block,
            block_size_nbytes,
            1,              // full blocks in row
            0,              // num leftover tiles
            0,              // leftover width in row
            row_start_id
        };
        // log_debug("reader: {},{} = {} {}", core.x, core.y, block_size_nbytes, row_start_id);

        // writer runtime args
        vector<uint32_t> writer_rt_args = {
            dst_buffer->address(),
            ntiles_per_block * nblocks_per_core_cliff,    // ntiles per core
            tile_start_id                           // start id
        };
        // log_debug("writer: {},{} = {} ({})", core.x, core.y, tile_start_id, ntiles_per_block * nblocks_per_core_cliff);

        SetRuntimeArgs(
            program,
            unary_reader_kernel_id,
            core,
            reader_rt_args
        );

        SetRuntimeArgs(
            program,
            unary_writer_kernel_id,
            core,
            writer_rt_args
        );
    }

    auto override_runtime_args_callback = [
        reader_kernel_id=unary_reader_kernel_id,
        writer_kernel_id=unary_writer_kernel_id,
        ncores=ncores,
        ncores_x=ncores_x
    ](
        const Program &program,
        const std::vector<Buffer*>& input_buffers,
        const std::vector<Buffer*>& output_buffers
    ) {
        auto src_buffer = input_buffers.at(0);
        auto dst_buffer = output_buffers.at(0);

        for (uint32_t i = 0; i < ncores; ++ i) {
            CoreCoord core = {i % ncores_x, i / ncores_x};
            {
                auto runtime_args = GetRuntimeArgs(program, reader_kernel_id, core);
                runtime_args[0] = src_buffer->address();
                SetRuntimeArgs(program, reader_kernel_id, core, runtime_args);
            }
            {
                auto runtime_args = GetRuntimeArgs(program, writer_kernel_id, core);
                runtime_args[0] = dst_buffer->address();
                SetRuntimeArgs(program, writer_kernel_id, core, runtime_args);
            }
        }
    };

    return {std::move(program), override_runtime_args_callback};
}

operation::ProgramWithCallbacks tilize_multi_core_sharded(const Tensor &input, Tensor &output) {
    tt_metal::Program program{};

    tt::DataFormat cb_data_format = tt_metal::datatype_to_dataformat_converter(input.dtype());
    uint32_t single_tile_size = tt_metal::detail::TileSize(cb_data_format);

    uint32_t num_tiles = input.volume() / TILE_HW;

    tt_metal::Device *device = input.device();

    auto shard_spec = input.shard_spec().value();
    uint32_t num_tiles_per_shard = shard_spec.shard_shape.first * shard_spec.shard_shape.second / TILE_HW;
    uint32_t num_tiles_per_row = shard_spec.shard_shape.second / TILE_WIDTH;
    auto all_cores = shard_spec.shard_grid;
    uint32_t num_cores_x = device->compute_with_storage_grid_size().x;
    uint32_t num_cores = 0;
    for (const auto& core_range : all_cores.ranges()) {
        num_cores += core_range.size();
    }

    uint32_t src0_cb_index = CB::c_in0;
    uint32_t num_input_tiles = num_tiles_per_shard;
    tt_metal::CircularBufferConfig src0_cb_config = tt_metal::CircularBufferConfig(num_input_tiles * single_tile_size, {{src0_cb_index, cb_data_format}})
		.set_page_size(src0_cb_index, single_tile_size).set_globally_allocated_address(input.buffer()->address());
	auto cb_src0 = tt_metal::CreateCircularBuffer(program, all_cores, src0_cb_config);

    uint32_t output_cb_index = CB::c_out0;
    uint32_t num_output_tiles = num_tiles_per_shard;
    tt_metal::CircularBufferConfig cb_output_config = tt_metal::CircularBufferConfig(num_output_tiles * single_tile_size, {{output_cb_index, cb_data_format}})
		.set_page_size(output_cb_index, single_tile_size).set_globally_allocated_address(output.buffer()->address());
    auto cb_output = tt_metal::CreateCircularBuffer(program, all_cores, cb_output_config);

    auto src_buffer = input.buffer();

    auto dst_buffer = output.buffer();

    std::vector<uint32_t> reader_compile_time_args = {
        (std::uint32_t) src0_cb_index
    };

    bool dst_is_dram = dst_buffer->buffer_type() == tt_metal::BufferType::DRAM ? 1 : 0;
    std::vector<uint32_t> writer_compile_time_args = {
        (std::uint32_t) output_cb_index
    };

    tt_metal::KernelID unary_reader_kernel_id = tt_metal::CreateDataMovementKernel(
        program,
        "tt_metal/kernels/dataflow/reader_unary_sharded.cpp",
        all_cores,
        tt_metal::DataMovementConfig{.processor = tt_metal::DataMovementProcessor::RISCV_1, .noc = tt_metal::NOC::RISCV_1_default, .compile_args = reader_compile_time_args});

    tt_metal::KernelID unary_writer_kernel_id = tt_metal::CreateDataMovementKernel(
        program,
        "tt_metal/kernels/dataflow/writer_unary_sharded.cpp",
        all_cores,
        tt_metal::DataMovementConfig{.processor = tt_metal::DataMovementProcessor::RISCV_0, .noc = tt_metal::NOC::RISCV_0_default, .compile_args = writer_compile_time_args});

    vector<uint32_t> compute_args = {
        uint32_t(num_tiles_per_shard / num_tiles_per_row),
        uint32_t(num_tiles_per_row)
    };

    auto untilize_kernel_id = tt_metal::CreateComputeKernel(
        program,
        "tt_metal/kernels/compute/tilize.cpp",
        all_cores,
        tt_metal::ComputeConfig{.compile_args = compute_args}
    );

    tt_metal::SetRuntimeArgs(
        program,
        unary_reader_kernel_id,
        all_cores,
        {
            num_tiles_per_shard
        }
    );

    tt_metal::SetRuntimeArgs(
        program,
        unary_writer_kernel_id,
        all_cores,
        {
            num_tiles_per_shard
        }
    );

    auto override_runtime_args_callback = [
            unary_reader_kernel_id,
            unary_writer_kernel_id,
            num_cores,
            num_cores_x
        ]
    (
        const Program &program,
        const std::vector<Buffer*>& input_buffers,
        const std::vector<Buffer*>& output_buffers
    ) {

    };

    return {std::move(program), override_runtime_args_callback};
}

operation::ProgramWithCallbacks tilize_multi_core(const Tensor& a, Tensor& output) {
    if (a.memory_config().is_sharded()) {
        return tilize_multi_core_sharded(a, output);
    } else {
        return tilize_multi_core_interleaved(a, output);
    }
}

}  // namespace tt_metal

}  // namespace tt