// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "gtest/gtest.h"
#include "tt_metal/detail/tt_metal.hpp"
#include "tt_metal/host_api.hpp"
#include "tt_metal/test_utils/env_vars.hpp"
#include "tt_metal/impl/dispatch/command_queue.hpp"

// A dispatch-agnostic test fixture
class CommonFixture: public ::testing::Test {
public:
    // A function to run a program, according to which dispatch mode is set.
    void RunProgram(Device* device, Program& program) {
        if (this->slow_dispatch_) {
            // Slow dispatch uses LaunchProgram
            tt::tt_metal::detail::LaunchProgram(device, program);
        } else {
            // Fast Dispatch uses the command queue
            CommandQueue& cq = tt::tt_metal::detail::GetCommandQueue(device);
            EnqueueProgram(cq, program, false);
            Finish(cq);
        }
    }

protected:
    tt::ARCH arch_;
    vector<Device*> devices_;
    bool slow_dispatch_;
    bool has_remote_devices_;

    void SetUp() override {
        // Skip for slow dispatch for now
        auto slow_dispatch = getenv("TT_METAL_SLOW_DISPATCH_MODE");
        if (slow_dispatch) {
            tt::log_info(tt::LogTest, "Running test using Slow Dispatch");
            slow_dispatch_ = true;
        } else {
            tt::log_info(tt::LogTest, "Running test using Fast Dispatch");
            slow_dispatch_ = false;
        }
        // Set up all available devices
        this->arch_ = tt::get_arch_from_string(tt::test_utils::get_env_arch_name());
        auto num_devices = tt::tt_metal::GetNumAvailableDevices();
        auto num_pci_devices = tt::tt_metal::GetNumPCIeDevices();
        // An extra flag for if we have remote devices, as some tests are disabled for fast
        // dispatch + remote devices.
        this->has_remote_devices_ = num_devices > num_pci_devices;
        for (unsigned int id = 0; id < num_devices; id++) {
            if (SkipTest(id))
                continue;
            auto* device = tt::tt_metal::CreateDevice(id);
            devices_.push_back(device);
        }
    }

    void TearDown() override {
        // Close all opened devices
        for (unsigned int id = 0; id < devices_.size(); id++) {
            tt::tt_metal::CloseDevice(devices_.at(id));
        }
    }

    bool SkipTest(unsigned int device_id) {
        // Skip condition is fast dispatch for remote devices
        if (this->has_remote_devices_ && !this->slow_dispatch_ && device_id != 0) {
            log_info(
                tt::LogTest,
                "Skipping test on device {} due to fast dispatch unsupported on remote devices.",
                device_id
            );
            return true;
        }

        // Also skip all devices after device 0 for grayskull. This to match all other tests
        // targetting device 0 by default (and to not cause issues with BMs that have E300s).
        // TODO: when we can detect only supported devices, this check can be removed.
        if (this->arch_ == tt::ARCH::GRAYSKULL && device_id > 0) {
            log_info(
                tt::LogTest,
                "Skipping test on device {} due to unsupported E300",
                device_id
            );
            return true;
        }

        return false;
    }

    void RunTestOnDevice(
        const std::function<void()>& run_function,
        Device* device
    ) {
        if (SkipTest(device->id()))
            return;
        log_info(tt::LogTest, "Running test on device {}.", device->id());
        run_function();
        log_info(tt::LogTest, "Finished running test on device {}.", device->id());
    }
};