#include <algorithm>
#include <functional>
#include <random>
#include <string>

#include "common/bfloat16.hpp"
#include "tt_metal/detail/tt_metal.hpp"
#include "tt_metal/host_api.hpp"

using namespace tt;
using namespace tt::tt_metal;
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::steady_clock;

int main(int argc, char** argv) {
  bool pass = true;

  try {
    // Initial Runtime Args Parse
    std::vector<std::string> input_args(argv, argv + argc);

    string arch_name = "";
    try {
      std::tie(arch_name, input_args) =
          test_args::get_command_option_and_remaining_args(input_args, "--arch",
                                                           "grayskull");
    } catch (const std::exception& e) {
      log_fatal(
          tt::LogTest,
          "Please input proper architecture name such as \"--arch grayskull\"",
          e.what());
    }
    const tt::ARCH arch = tt::get_arch_from_string(arch_name);

    string size_string = "";
    try {
      std::tie(size_string, input_args) =
          test_args::get_command_option_and_remaining_args(input_args,
                                                           "--size");
    } catch (const std::exception& e) {
      log_fatal(tt::LogTest,
                "Please input test size with \"--size <size to test>\"",
                e.what());
    }
    uint64_t buffer_size = stoul(size_string);

    log_info(LogTest, "Measuring performance for size={}bytes", buffer_size);

    // Device Setup
    int pci_express_slot = 0;
    tt_metal::Device* device = tt_metal::CreateDevice(arch, pci_express_slot);
    pass &= tt_metal::InitializeDevice(device);

    // Application Setup
    srand(time(0));
    size_t core_x = rand() % 8;
    size_t core_y = rand() % 8;
    log_info(LogTest, "Target core (x,y) = ({},{})", core_x, core_y);
    CoreCoord core = {core_x, core_y};

    // Can accomodate input size up to 920*1024
    uint32_t target_cb_addr = 107 * 1024;

    // Execute Application
    std::vector<uint32_t> src_vec = create_random_vector_of_bfloat16(
        buffer_size, 100,
        std::chrono::system_clock::now().time_since_epoch().count());

    {
      auto begin = std::chrono::steady_clock::now();
      pass &= tt_metal::detail::WriteToDeviceL1(device, core, target_cb_addr,
                                                src_vec);
      auto end = std::chrono::steady_clock::now();
      auto elapsed_us = duration_cast<microseconds>(end - begin).count();
      auto bw = (buffer_size / 1024.0 / 1024.0 / 1024.0) /
                (elapsed_us / 1000.0 / 1000.0);
      log_info(LogTest, "WriteToDeviceL1 {:.3f}ms, {:.3f}GB/s",
               elapsed_us / 1000.0, bw);
    }

    std::vector<uint32_t> result_vec;
    {
      auto begin = std::chrono::steady_clock::now();
      tt_metal::detail::ReadFromDeviceL1(device, core, target_cb_addr,
                                         buffer_size, result_vec);
      auto end = std::chrono::steady_clock::now();

      auto elapsed_us = duration_cast<microseconds>(end - begin).count();
      auto bw = (buffer_size / 1024.0 / 1024.0 / 1024.0) /
                (elapsed_us / 1000.0 / 1000.0);
      log_info(LogTest, "ReadFromDeviceL1 {:.3f}ms, {:.3f}GB/s",
               elapsed_us / 1000.0, bw);
    }

    // Validation & Teardown
    pass &= (src_vec == result_vec);
    pass &= tt_metal::CloseDevice(device);
  } catch (const std::exception& e) {
    pass = false;
    log_error(LogTest, "{}", e.what());
    log_error(LogTest, "System error message: {}", std::strerror(errno));
  }

  if (pass) {
    log_info(LogTest, "Test Passed");
  } else {
    log_fatal(LogTest, "Test Failed");
  }

  TT_ASSERT(pass);
  return 0;
}