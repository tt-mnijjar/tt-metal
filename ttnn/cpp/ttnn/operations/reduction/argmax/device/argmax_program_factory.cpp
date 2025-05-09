// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include "ttnn/operations/math.hpp"
#include <tt-metalium/work_split.hpp>
#include <tt-metalium/constants.hpp>
#include <tt-metalium/util.hpp>
#include <tt-metalium/host_api.hpp>
#include "ttnn/operation.hpp"

using namespace tt::tt_metal;

namespace ttnn::operations::reduction::detail {

using namespace tt::constants;

operation::ProgramWithCallbacks argmax_single_core(
    const Tensor& input, const Tensor& output, const std::optional<uint32_t> dim, const bool keepdim) {
    tt::tt_metal::Program program{};

    const tt::DataFormat input_cb_data_format = tt::tt_metal::datatype_to_dataformat_converter(input.get_dtype());
    const uint32_t input_unit_size = input.element_size();
    const tt::DataFormat output_cb_data_format = tt::tt_metal::datatype_to_dataformat_converter(output.get_dtype());
    const uint32_t output_unit_size = output.element_size();

    const tt::tt_metal::IDevice* device = output.device();

    const auto compute_with_storage_grid_size = device->compute_with_storage_grid_size();
    uint32_t num_cores_x = compute_with_storage_grid_size.x;
    uint32_t num_cores_y = compute_with_storage_grid_size.y;
    uint32_t num_units = 1;  // single-core
    auto [num_cores, all_cores, core_group_1, core_group_2, num_units_per_core_group_1, num_units_per_core_group_2] =
        tt::tt_metal::split_work_to_cores(compute_with_storage_grid_size, num_units);

    const auto& input_shape = input.get_padded_shape();
    const uint32_t rank = input_shape.size();
    const bool reduce_all = not dim.has_value();

    // Last dimension in input i.e. reduction dimension
    const uint32_t red_dim_units = input_shape[rank - 1];

    // Last dimension in output i.e. the dim left after reduction
    const auto output_last_dim = reduce_all or keepdim or (rank < 2) ? 1 : input_shape[rank - 2];

    // Create input CB to read reduction dim worth of data at once
    const uint32_t src_cb_idx = tt::CBIndex::c_0;
    const uint32_t src_page_size = round_up_to_mul32(red_dim_units * input_unit_size);
    tt::tt_metal::CircularBufferConfig src_cb_config =
        tt::tt_metal::CircularBufferConfig(src_page_size, {{src_cb_idx, input_cb_data_format}})
            .set_page_size(src_cb_idx, src_page_size);
    const auto src_cb = tt::tt_metal::CreateCircularBuffer(program, all_cores, src_cb_config);

    // Create output CB based on the output shape's last dimension
    const uint32_t dst_cb_idx = tt::CBIndex::c_1;
    const uint32_t dst_page_size = round_up_to_mul32(output_last_dim * output_unit_size);
    const tt::tt_metal::CircularBufferConfig dst_db_config =
        tt::tt_metal::CircularBufferConfig(dst_page_size, {{dst_cb_idx, output_cb_data_format}})
            .set_page_size(dst_cb_idx, dst_page_size);
    const auto dst_cb = tt::tt_metal::CreateCircularBuffer(program, all_cores, dst_db_config);

    const auto src_buffer = input.buffer();
    const auto dst_buffer = output.buffer();
    const bool src_is_dram = src_buffer->buffer_type() == tt::tt_metal::BufferType::DRAM;
    const bool dst_is_dram = dst_buffer->buffer_type() == tt::tt_metal::BufferType::DRAM;

    const auto inner_dim_units = output_last_dim;
    const auto outer_dim_units = input.get_logical_volume() / inner_dim_units / red_dim_units;

    const std::vector<uint32_t> reader_compile_time_args = {
        src_cb_idx,
        dst_cb_idx,
        src_is_dram,
        dst_is_dram,
        src_page_size,
        dst_page_size,
        outer_dim_units,
        inner_dim_units,
        red_dim_units,
        (uint32_t)(reduce_all),
    };

    const std::map<string, string> kernel_defines;
    const tt::tt_metal::KernelHandle reader_kernel_id = tt::tt_metal::CreateKernel(
        program,
        "ttnn/cpp/ttnn/operations/reduction/argmax/device/kernels/reader_argmax_interleaved.cpp",
        all_cores,
        tt::tt_metal::ReaderDataMovementConfig(reader_compile_time_args, kernel_defines));

    const auto cores = grid_to_cores(num_cores, num_cores_x, num_cores_y, false);

    for (uint32_t i = 0; i < cores.size(); ++i) {
        const CoreCoord& core = cores.at(i);

        tt::tt_metal::SetRuntimeArgs(program, reader_kernel_id, core, {src_buffer->address(), dst_buffer->address()});
    }

    auto override_runtime_args_callback = [reader_kernel_id, cores](
                                              const void* operation,
                                              const Program& program,
                                              const std::vector<Tensor>& input_tensors,
                                              const std::vector<std::optional<const Tensor>>&,
                                              const std::vector<Tensor>& output_tensors) {
        auto src_buffer = input_tensors.at(0).buffer();

        auto dst_buffer = output_tensors.at(0).buffer();

        for (const auto& core : cores) {
            {
                auto& runtime_args = GetRuntimeArgs(program, reader_kernel_id, core);
                runtime_args[0] = src_buffer->address();
                runtime_args[1] = dst_buffer->address();
            }
        }
    };

    return {std::move(program), override_runtime_args_callback};
}

operation::ProgramWithCallbacks argmax_multi_core(
    const Tensor& input,
    const Tensor& output,
    const std::optional<uint32_t> dim,
    const std::optional<CoreRangeSet>& sub_core_grids) {
    tt::tt_metal::Program program{};

    tt::DataFormat input_cb_data_format = tt::tt_metal::datatype_to_dataformat_converter(input.get_dtype());
    uint32_t input_unit_size = input.element_size();
    tt::DataFormat output_cb_data_format = tt::tt_metal::datatype_to_dataformat_converter(output.get_dtype());
    uint32_t output_unit_size = output.element_size();

    tt::tt_metal::IDevice* device = output.device();

    auto compute_with_storage_grid_size = device->compute_with_storage_grid_size();
    uint32_t num_cores_x = compute_with_storage_grid_size.x;
    uint32_t num_cores_y = compute_with_storage_grid_size.y;
    CoreRangeSet core_grid = CoreRangeSet(std::vector{CoreRange({0, 0}, {num_cores_x - 1, num_cores_y - 1})});
    uint32_t num_cores = num_cores_x * num_cores_y;

    if (sub_core_grids.has_value()) {
        core_grid = sub_core_grids.value();
        num_cores = core_grid.num_cores();
    }

    const auto& input_shape = input.get_padded_shape();
    const uint32_t B = input_shape[0];
    const uint32_t C = input_shape[1];
    const uint32_t H = input_shape[2];
    const uint32_t W = input_shape[3];

    uint32_t src0_cb_index = tt::CBIndex::c_0;
    uint32_t num_input_units = W;
    uint32_t aligned_input_unit_size = round_up_to_mul32(num_input_units * input_unit_size);
    tt::tt_metal::CircularBufferConfig cb_src0_config =
        tt::tt_metal::CircularBufferConfig(aligned_input_unit_size, {{src0_cb_index, input_cb_data_format}})
            .set_page_size(src0_cb_index, aligned_input_unit_size);
    auto cb_src0 = tt::tt_metal::CreateCircularBuffer(program, core_grid, cb_src0_config);

    uint32_t intermed0_cb_index = tt::CBIndex::c_1;
    uint32_t num_intermed0_units = B * C * H;
    uint32_t aligned_intermed0_unit_size = num_intermed0_units * output_unit_size;
    tt::tt_metal::CircularBufferConfig intermed0_cb_config =
        tt::tt_metal::CircularBufferConfig(aligned_intermed0_unit_size, {{intermed0_cb_index, output_cb_data_format}})
            .set_page_size(intermed0_cb_index, aligned_intermed0_unit_size);  /// page size shouldn't matter here
    auto cb_intermed0 = tt::tt_metal::CreateCircularBuffer(program, core_grid, intermed0_cb_config);

    uint32_t intermed1_cb_index = tt::CBIndex::c_2;
    uint32_t num_intermed1_units = B * C * H;
    uint32_t aligned_intermed1_unit_size = round_up_to_mul32(num_intermed1_units * input_unit_size);
    tt::tt_metal::CircularBufferConfig intermed1_cb_config =
        tt::tt_metal::CircularBufferConfig(aligned_intermed1_unit_size, {{intermed1_cb_index, input_cb_data_format}})
            .set_page_size(intermed1_cb_index, aligned_intermed1_unit_size);  /// page size shouldn't matter here
    auto cb_intermed1 = tt::tt_metal::CreateCircularBuffer(program, core_grid, intermed1_cb_config);

    uint32_t out0_cb_index = tt::CBIndex::c_3;
    uint32_t num_out0_units = B * C * H;
    uint32_t aligned_out0_unit_size = num_out0_units * output_unit_size;
    tt::tt_metal::CircularBufferConfig out0_cb_config =
        tt::tt_metal::CircularBufferConfig(aligned_out0_unit_size, {{out0_cb_index, output_cb_data_format}})
            .set_page_size(out0_cb_index, aligned_out0_unit_size);  /// page size shouldn't matter here
    auto cb_out0 = tt::tt_metal::CreateCircularBuffer(program, core_grid, out0_cb_config);

    auto src_buffer = input.buffer();
    auto dst_buffer = output.buffer();
    bool src_is_dram = src_buffer->buffer_type() == tt::tt_metal::BufferType::DRAM;
    bool dst_is_dram = dst_buffer->buffer_type() == tt::tt_metal::BufferType::DRAM;

    auto semaphore_addr = tt::tt_metal::CreateSemaphore(program, core_grid, 0);
    auto cores = corerange_to_cores(core_grid, num_cores, true);
    CoreCoord final_cores_physical = device->worker_core_from_logical_core(cores.at(0));
    uint32_t final_cores_physical_x = final_cores_physical.x;
    uint32_t final_cores_physical_y = final_cores_physical.y;

    std::vector<uint32_t> reader_compile_time_args = {
        src0_cb_index,
        intermed0_cb_index,
        intermed1_cb_index,
        out0_cb_index,
        src_is_dram,
        dst_is_dram,
        aligned_input_unit_size,
        aligned_intermed0_unit_size,
        aligned_out0_unit_size,
        B,
        C,
        H,
        W / num_cores,
        num_cores,
        semaphore_addr,
        final_cores_physical_x,
        final_cores_physical_y};

    std::map<string, string> kernel_defines;
    tt::tt_metal::KernelHandle reader_kernel_id = tt::tt_metal::CreateKernel(
        program,
        "ttnn/cpp/ttnn/operations/reduction/argmax/device/kernels/reader_argmax_interleaved_multicore.cpp",
        core_grid,
        tt::tt_metal::DataMovementConfig{
            .processor = tt::tt_metal::DataMovementProcessor::RISCV_1,
            .noc = tt::tt_metal::NOC::RISCV_1_default,
            .compile_args = reader_compile_time_args});

    // tt::tt_metal::KernelHandle writer_kernel_id = tt::tt_metal::CreateKernel(
    //     program,
    //     "ttnn/cpp/ttnn/operations/reduction/argmax/device/kernels/reader_argmax_interleaved_multicore.cpp",
    //     all_cores,
    //     tt::tt_metal::DataMovementConfig{.processor = tt::tt_metal::DataMovementProcessor::RISCV_0, .noc =
    //     tt::tt_metal::NOC::RISCV_0_default, .compile_args = reader_compile_time_args});

    for (uint32_t i = 0; i < cores.size(); ++i) {
        const CoreCoord& core = cores.at(i);

        tt::tt_metal::SetRuntimeArgs(
            program, reader_kernel_id, core, {src_buffer->address(), dst_buffer->address(), i});

        // tt::tt_metal::SetRuntimeArgs(program, reader_kernel_id, core, {src_buffer->address(), dst_buffer->address(),
        // 2*i+1});
    }

    auto override_runtime_args_callback = [reader_kernel_id, cores](
                                              const void* operation,
                                              const Program& program,
                                              const std::vector<Tensor>& input_tensors,
                                              const std::vector<std::optional<const Tensor>>&,
                                              const std::vector<Tensor>& output_tensors) {
        auto src_buffer = input_tensors.at(0).buffer();

        auto dst_buffer = output_tensors.at(0).buffer();
        uint32_t core_id = 0;
        for (const auto& core : cores) {
            {
                auto& reader_runtime_args = GetRuntimeArgs(program, reader_kernel_id, core);
                reader_runtime_args[0] = src_buffer->address();
                reader_runtime_args[1] = dst_buffer->address();
                reader_runtime_args[2] = core_id;
                core_id++;

                // auto &writer_runtime_args = GetRuntimeArgs(program, writer_kernel_id, core);
                // writer_runtime_args[0] = src_buffer->address();
                // writer_runtime_args[1] = dst_buffer->address();
                // writer_runtime_args[2] = core_id;
                // core_id++;
            }
        }
    };

    return {std::move(program), override_runtime_args_callback};
}

}  // namespace ttnn::operations::reduction::detail
