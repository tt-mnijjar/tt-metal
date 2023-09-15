// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "single_device_fixture.hpp"
#include "gtest/gtest.h"
#include "tt_metal/test_utils/comparison.hpp"
#include "tt_metal/test_utils/df/df.hpp"
#include "tt_metal/test_utils/print_helpers.hpp"
#include "tt_metal/test_utils/stimulus.hpp"
#include "tt_metal/common/logger.hpp"
#include "tt_metal/common/math.hpp"
#include "tt_metal/host_api.hpp"
#include "tt_metal/detail/tt_metal.hpp"


using namespace tt::tt_metal;

namespace basic_tests::l1::banked {

struct BankedL1Config {
    size_t num_tiles = 1;
    size_t size_bytes = 1 * 2 * 32 * 32;
    size_t page_size_bytes = 2 * 32 * 32;
    BufferType input_buffer_type = BufferType::L1;
    BufferType output_buffer_type = BufferType::L1;
    CoreCoord logical_core = CoreCoord({.x = 0, .y = 0});
    tt::DataFormat l1_data_format = tt::DataFormat::Float16_b;
};

namespace local_test_functions {

/// @brief Does L1 --> Direct/Banked Reader --> CB --> Direct/Banked Writer --> L1 on a single core
/// @param device
/// @param test_config - Configuration of the test -- see struct
/// @return
bool l1_reader_cb_writer_l1(Device* device, const BankedL1Config& cfg, const bool banked_reader, const bool banked_writer) {
    bool pass = true;

    const uint32_t cb_id = 0;
    ////////////////////////////////////////////////////////////////////////////
    //                      Application Setup
    ////////////////////////////////////////////////////////////////////////////
    Program program = Program();

    string reader_kernel_name = "";
    string writer_kernel_name = "";
    size_t input_page_size_bytes = 0;
    size_t output_page_size_bytes = 0;
    std::vector<uint32_t> reader_runtime_args = {};
    std::vector<uint32_t> writer_runtime_args = {};
    if (banked_reader) {
        reader_kernel_name = "tests/tt_metal/tt_metal/test_kernels/banked_reader.cpp";
        input_page_size_bytes = cfg.page_size_bytes;
    } else {
        reader_kernel_name = "tests/tt_metal/tt_metal/test_kernels/direct_reader_unary.cpp";
        input_page_size_bytes = cfg.size_bytes;
    }

    if (banked_writer) {
        writer_kernel_name = "tests/tt_metal/tt_metal/test_kernels/banked_writer.cpp";
        output_page_size_bytes = cfg.page_size_bytes;
    } else {
        writer_kernel_name = "tests/tt_metal/tt_metal/test_kernels/direct_writer_unary.cpp";
        output_page_size_bytes = cfg.size_bytes;
    }

    Buffer input_buffer = Buffer(device, cfg.size_bytes, input_page_size_bytes, cfg.input_buffer_type);

    Buffer output_buffer = Buffer(device, cfg.size_bytes, output_page_size_bytes, cfg.output_buffer_type);

    tt::log_debug(tt::LogTest, "Input buffer: [address: {} B, size: {} B] at noc coord {}", input_buffer.address(), input_buffer.size(), input_buffer.noc_coordinates().str());
    tt::log_debug(tt::LogTest, "Output buffer: [address: {} B, size: {} B] at noc coord {}", output_buffer.address(), output_buffer.size(), output_buffer.noc_coordinates().str());

    TT_ASSERT(cfg.num_tiles * cfg.page_size_bytes == cfg.size_bytes);
    constexpr uint32_t num_pages_cb = 1;
    auto input_buffer_cb = CreateCircularBuffer(program, cb_id, cfg.logical_core, num_pages_cb, cfg.page_size_bytes, cfg.l1_data_format);

    bool input_is_dram = cfg.input_buffer_type == BufferType::DRAM;
    bool output_is_dram = cfg.output_buffer_type == BufferType::DRAM;

    auto reader_kernel = CreateDataMovementKernel(
        program,
        reader_kernel_name,
        cfg.logical_core,
        DataMovementConfig{.processor = DataMovementProcessor::RISCV_0, .noc = NOC::NOC_0, .compile_args = {cb_id, uint32_t(input_buffer.page_size()), (uint32_t)input_is_dram}});
    auto writer_kernel = CreateDataMovementKernel(
        program,
        writer_kernel_name,
        cfg.logical_core,
        DataMovementConfig{.processor = DataMovementProcessor::RISCV_1, .noc = NOC::NOC_1, .compile_args = {cb_id, uint32_t(output_buffer.page_size()), (uint32_t)output_is_dram}});

    if (banked_reader) {
        reader_runtime_args = {
            (uint32_t)input_buffer.address(),
            (uint32_t)cfg.num_tiles
        };
    } else {
        reader_runtime_args = {
            (uint32_t)input_buffer.address(),
            (uint32_t)input_buffer.noc_coordinates().x,
            (uint32_t)input_buffer.noc_coordinates().y,
            (uint32_t)cfg.num_tiles,
        };
    }
    if (banked_writer) {
        writer_runtime_args = {
            (uint32_t)output_buffer.address(),
            (uint32_t)cfg.num_tiles
        };
    } else {
        writer_runtime_args = {
            (uint32_t)output_buffer.address(),
            (uint32_t)output_buffer.noc_coordinates().x,
            (uint32_t)output_buffer.noc_coordinates().y,
            (uint32_t)cfg.num_tiles,
        };
    }
    ////////////////////////////////////////////////////////////////////////////
    //                      Compile and Execute Application
    ////////////////////////////////////////////////////////////////////////////
    pass &= CompileProgram(device, program);
    auto input_packed = tt::test_utils::generate_uniform_random_vector<uint32_t>(0, 100, cfg.size_bytes / sizeof(uint32_t));
    WriteToBuffer(input_buffer, input_packed);

    pass &= ConfigureDeviceWithProgram(device, program);
    SetRuntimeArgs(program, reader_kernel, cfg.logical_core, reader_runtime_args);
    SetRuntimeArgs(program, writer_kernel, cfg.logical_core, writer_runtime_args);
    WriteRuntimeArgsToDevice(device, program);

    pass &= LaunchKernels(device, program);

    std::vector<uint32_t> reread_input_packed;
    ReadFromBuffer(input_buffer, reread_input_packed);

    std::vector<uint32_t> output_packed;
    ReadFromBuffer(output_buffer, output_packed);

    pass &= (output_packed == input_packed);

    return pass;
}

/// @brief Does Interleaved L1 --> Reader --> CB --> Datacopy --> CB --> Writer --> Interleaved L1 on a single core
/// @param device
/// @param test_config - Configuration of the test -- see struct
/// @return
bool l1_reader_datacopy_l1_writer(Device* device, const BankedL1Config& cfg) {
    bool pass = true;

    const uint32_t input0_cb_index = 0;
    const uint32_t output_cb_index = 16;
    ////////////////////////////////////////////////////////////////////////////
    //                      Application Setup
    ////////////////////////////////////////////////////////////////////////////
    Program program = Program();
    auto input_buffer = Buffer(device, cfg.size_bytes, cfg.page_size_bytes, BufferType::L1);
    auto output_buffer = Buffer(device, cfg.size_bytes, cfg.page_size_bytes, BufferType::L1);

    std::cout << "Input buffer addr " << input_buffer.address() << " size: " << input_buffer.size() << " page size " << input_buffer.page_size()
              << " output buffer addr " << output_buffer.address() << " size: " << output_buffer.size() << " page size " << output_buffer.page_size() << std::endl;

    auto l1_input_cb = CreateCircularBuffer(
        program,
        input0_cb_index,
        cfg.logical_core,
        cfg.num_tiles,
        cfg.page_size_bytes,
        cfg.l1_data_format
    );
    auto l1_output_cb = CreateCircularBuffer(
        program,
        output_cb_index,
        cfg.logical_core,
        cfg.num_tiles,
        cfg.page_size_bytes,
        cfg.l1_data_format
    );

    std::cout << "input cb addr " << l1_input_cb.address() << " output cb address " << l1_output_cb.address() << std::endl;

    bool input_is_dram = cfg.input_buffer_type == BufferType::DRAM;
    bool output_is_dram = cfg.output_buffer_type == BufferType::DRAM;

    auto reader_kernel = CreateDataMovementKernel(
        program,
        "tests/tt_metal/tt_metal/test_kernels/banked_reader.cpp",
        cfg.logical_core,
        DataMovementConfig{
            .processor = DataMovementProcessor::RISCV_1,
            .noc = NOC::RISCV_1_default,
            .compile_args = {input0_cb_index, uint32_t(cfg.page_size_bytes), uint32_t(input_is_dram)}});

    auto writer_kernel = CreateDataMovementKernel(
        program,
        "tests/tt_metal/tt_metal/test_kernels/banked_writer.cpp",
        cfg.logical_core,
        DataMovementConfig{
            .processor = DataMovementProcessor::RISCV_0,
            .noc = NOC::RISCV_0_default,
            .compile_args = {output_cb_index, uint32_t(cfg.page_size_bytes), uint32_t(output_is_dram)}});

    vector<uint32_t> compute_kernel_args = {
        uint(cfg.num_tiles)  // per_core_tile_cnt
    };
    auto datacopy_kernel = CreateComputeKernel(
        program,
        "tt_metal/kernels/compute/eltwise_copy.cpp",
        cfg.logical_core,
        ComputeConfig{.compile_args = compute_kernel_args});

    ////////////////////////////////////////////////////////////////////////////
    //                      Stimulus Generation
    ////////////////////////////////////////////////////////////////////////////
    auto input_packed = tt::test_utils::generate_uniform_random_vector<uint32_t>(0, 100, cfg.size_bytes / sizeof(uint32_t));
    ////////////////////////////////////////////////////////////////////////////
    //                      Compile and Execute Application
    ////////////////////////////////////////////////////////////////////////////
    auto noc_coord = device->worker_core_from_logical_core(cfg.logical_core);

    // StartDebugPrintServer(device, {CoreCoord(1, 1), noc_coord});
    pass &= CompileProgram(device, program);
    WriteToBuffer(input_buffer, input_packed);

    pass &= ConfigureDeviceWithProgram(device, program);
    SetRuntimeArgs(
        program,
        reader_kernel,
        cfg.logical_core,
        {
            (uint32_t)input_buffer.address(),
            (uint32_t)cfg.num_tiles,
        });
    SetRuntimeArgs(
        program,
        writer_kernel,
        cfg.logical_core,
        {
            (uint32_t)output_buffer.address(),
            (uint32_t)cfg.num_tiles,
        });
    WriteRuntimeArgsToDevice(device, program);
    pass &= LaunchKernels(device, program);

    std::vector<uint32_t> dest_buffer_data;
    ReadFromBuffer(output_buffer, dest_buffer_data);
    pass &= input_packed == dest_buffer_data;

    std::vector<uint32_t> cb_data;
    detail::ReadFromDeviceL1(device, cfg.logical_core, l1_input_cb.address(), l1_input_cb.size(), cb_data);

    std::vector<uint32_t> o_cb_data;
    detail::ReadFromDeviceL1(device, cfg.logical_core, l1_output_cb.address(), l1_output_cb.size(), o_cb_data);

    if (not pass) {
        std::cout << "------- INPUT PACKED -------" << std::endl;
        tt::test_utils::print_vector_fixed_numel_per_row(input_packed, 32);
        std::cout << "------- INPUT CB DATA -------" << std::endl;
        tt::test_utils::print_vector_fixed_numel_per_row(cb_data, 32);
        std::cout << "------- OUTPUT CB DATA -------" << std::endl;
        tt::test_utils::print_vector_fixed_numel_per_row(o_cb_data, 32);
        std::cout << "------- OUTPUT PACKED -------" << std::endl;
        tt::test_utils::print_vector_fixed_numel_per_row(dest_buffer_data, 32);

        // std::cout << "------- L1 -------" << std::endl;
        // std::vector<uint32_t> l1_data;
        // detail::ReadFromDeviceL1(device, cfg.logical_core, 0, device->l1_size(), l1_data);
        // tt::test_utils::print_vector_fixed_numel_per_row(l1_data, 32);

    }

    return pass;
}

}   // end namespace local_test_functions

TEST_F(SingleDeviceFixture, TestSingleCoreSingleTileBankedL1ReaderOnly) {
    BankedL1Config test_config;
    EXPECT_TRUE(local_test_functions::l1_reader_cb_writer_l1(this->device_, test_config, true, false));
}

TEST_F(SingleDeviceFixture, TestSingleCoreMultiTileBankedL1ReaderOnly) {
    BankedL1Config test_config;
    TT_ASSERT(this->device_->num_banks(BufferType::L1) % 2 == 0);
    size_t num_tiles = this->device_->num_banks(BufferType::L1) / 2;
    size_t tile_increment = num_tiles;
    u32 num_iterations = 3;
    u32 index = 0;
    while (index < num_iterations) {
        test_config.num_tiles = num_tiles;
        test_config.size_bytes = test_config.num_tiles * 2 * 32 * 32;
        EXPECT_TRUE(local_test_functions::l1_reader_cb_writer_l1(this->device_, test_config, true, false));
        num_tiles += tile_increment;
        index++;
    }
}

TEST_F(SingleDeviceFixture, TestSingleCoreSingleTileBankedL1WriterOnly) {
    BankedL1Config test_config;
    EXPECT_TRUE(local_test_functions::l1_reader_cb_writer_l1(this->device_, test_config, false, true));
}

TEST_F(SingleDeviceFixture, TestSingleCoreMultiTileBankedL1WriterOnly) {
    BankedL1Config test_config;
    TT_ASSERT(this->device_->num_banks(BufferType::L1) % 2 == 0);
    size_t num_tiles = this->device_->num_banks(BufferType::L1) / 2;
    size_t tile_increment = num_tiles;
    u32 num_iterations = 3;
    u32 index = 0;
    while (index < num_iterations) {
        test_config.num_tiles = num_tiles;
        test_config.size_bytes = test_config.num_tiles * 2 * 32 * 32;
        EXPECT_TRUE(local_test_functions::l1_reader_cb_writer_l1(this->device_, test_config, false, true));
        num_tiles += tile_increment;
        index++;
    }
}

TEST_F(SingleDeviceFixture, TestSingleCoreSingleTileBankedL1ReaderAndWriter) {
    BankedL1Config test_config;
    EXPECT_TRUE(local_test_functions::l1_reader_cb_writer_l1(this->device_, test_config, true, true));
}

TEST_F(SingleDeviceFixture, TestSingleCoreMultiTileBankedL1ReaderAndWriter) {
    BankedL1Config test_config;
    size_t num_tiles = this->device_->num_banks(BufferType::L1);
    TT_ASSERT(num_tiles % 2 == 0);
    size_t tile_increment = num_tiles / 2;
    u32 num_iterations = 6;
    u32 index = 0;
    while (index < num_iterations) {
        test_config.num_tiles = num_tiles;
        test_config.size_bytes = test_config.num_tiles * 2 * 32 * 32;
        EXPECT_TRUE(local_test_functions::l1_reader_cb_writer_l1(this->device_, test_config, true, true));
        num_tiles += tile_increment;
        index++;
    }
}

TEST_F(SingleDeviceFixture, TestSingleCoreSingleTileBankedDramReaderAndL1Writer) {
    BankedL1Config test_config;
    test_config.input_buffer_type = BufferType::DRAM;
    EXPECT_TRUE(local_test_functions::l1_reader_cb_writer_l1(this->device_, test_config, true, true));
}

TEST_F(SingleDeviceFixture, TestSingleCoreMultiTileBankedDramReaderAndL1Writer) {
    BankedL1Config test_config;
    test_config.input_buffer_type = BufferType::DRAM;

    size_t num_tiles = this->device_->num_banks(BufferType::L1);
    TT_ASSERT(num_tiles % 2 == 0);
    size_t tile_increment = num_tiles / 2;
    u32 num_iterations = 6;
    u32 index = 0;
    while (index < num_iterations) {
        test_config.num_tiles = num_tiles;
        test_config.size_bytes = test_config.num_tiles * 2 * 32 * 32;
        EXPECT_TRUE(local_test_functions::l1_reader_cb_writer_l1(this->device_, test_config, true, true));
        num_tiles += tile_increment;
        index++;
    }
}

TEST_F(SingleDeviceFixture, TestSingleCoreSingleTileBankedL1ReaderAndDramWriter) {
    BankedL1Config test_config;
    test_config.output_buffer_type = BufferType::DRAM;
    EXPECT_TRUE(local_test_functions::l1_reader_cb_writer_l1(this->device_, test_config, true, true));
}

TEST_F(SingleDeviceFixture, TestSingleCoreMultiTileBankedL1ReaderAndDramWriter) {
    BankedL1Config test_config;
    test_config.output_buffer_type = BufferType::DRAM;

    size_t num_tiles = this->device_->num_banks(BufferType::L1);
    TT_ASSERT(num_tiles % 2 == 0);
    size_t tile_increment = num_tiles / 2;
    u32 num_iterations = 6;
    u32 index = 0;
    while (index < num_iterations) {
        test_config.num_tiles = num_tiles;
        test_config.size_bytes = test_config.num_tiles * 2 * 32 * 32;
        EXPECT_TRUE(local_test_functions::l1_reader_cb_writer_l1(this->device_, test_config, true, true));
        num_tiles += tile_increment;
        index++;
    }
}

// TODO (abhullar): This test is related to #2283
// https://github.com/tenstorrent-metal/tt-metal/issues/2283
TEST_F(SingleDeviceFixture, DISABLED_TestSingleCoreMultiTileBankedL1ReaderDataCopyL1Writer) {
    BankedL1Config test_config;
    // size_t num_tiles = this->device_->num_banks(BufferType::L1);
    // TT_ASSERT(num_tiles % 2 == 0);
    size_t num_tiles = 1;
    size_t tile_increment = num_tiles / 2;
    u32 num_iterations = 1;
    //u32 num_iterations = 6;
    u32 index = 0;
    test_config.logical_core = this->device_->logical_core_from_bank_id(0);
    while (index < num_iterations) {
        test_config.num_tiles = num_tiles;
        test_config.size_bytes = test_config.num_tiles * 2 * 32 * 32;
        EXPECT_TRUE(local_test_functions::l1_reader_datacopy_l1_writer(this->device_, test_config));
        num_tiles += tile_increment;
        index++;
    }
}

}   // end namespace basic_tests::l1::banked