#include "common/constants.hpp"
#include "tensor/owned_buffer.hpp"
#include "tensor/owned_buffer_functions.hpp"
#include "tensor/tensor.hpp"
#include "tt_dnn/op_library/eltwise_binary/eltwise_binary_op.hpp"
#include "tt_dnn/op_library/program_cache.hpp"
#include "tt_numpy/functions.hpp"

using tt::tt_metal::DataType;
using tt::tt_metal::Device;
using tt::tt_metal::Layout;
using tt::tt_metal::Tensor;
using tt::tt_metal::OwnedStorage;
using tt::tt_metal::Shape;

template <typename BinaryFunction>
Tensor host_function(const Tensor& input_tensor_a, const Tensor& input_tensor_b) {
    auto input_a_buffer = tt::tt_metal::owned_buffer::get_as<bfloat16>(input_tensor_a);
    auto input_b_buffer = tt::tt_metal::owned_buffer::get_as<bfloat16>(input_tensor_b);

    auto output_buffer = tt::tt_metal::owned_buffer::create<bfloat16>(input_tensor_a.volume());

    for (auto index = 0; index < output_buffer.size(); index++) {
        auto value = BinaryFunction{}(input_a_buffer[index].to_float(), input_b_buffer[index].to_float());
        output_buffer[index] = bfloat16(value);
    }
    return Tensor(OwnedStorage{output_buffer}, input_tensor_a.shape(), input_tensor_a.dtype(), input_tensor_a.layout());
}

template <auto HostFunction, typename DeviceFunction, typename... Args>
bool run_test(const DeviceFunction& device_function, Device* device, Args... args) {
    Shape shape = {1, 1, tt::constants::TILE_HEIGHT, tt::constants::TILE_WIDTH};
    auto input_tensor_a = tt::numpy::random::random(shape, DataType::BFLOAT16).to(Layout::TILE);
    auto input_tensor_b = tt::numpy::random::random(shape, DataType::BFLOAT16).to(Layout::TILE);

    auto host_output = HostFunction(input_tensor_a, input_tensor_b);
    auto device_output = device_function(input_tensor_a.to(device), input_tensor_b.to(device)).cpu();

    return tt::numpy::allclose<bfloat16>(host_output, device_output, args...);
}

int main() {
    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    int pci_express_slot = 0;
    auto device = tt::tt_metal::CreateDevice(tt::ARCH::GRAYSKULL, pci_express_slot);

    TT_ASSERT(tt::tt_metal::InitializeDevice(device));

    {
        auto allclose = run_test<host_function<std::plus<float>>>(tt::tt_metal::add, device);
        TT_ASSERT(allclose);
    }

    {
        auto allclose = run_test<host_function<std::minus<float>>>(tt::tt_metal::sub, device);
        TT_ASSERT(allclose);
    }

    {
        auto allclose = run_test<host_function<std::multiplies<float>>>(tt::tt_metal::mul, device, 1e-2f, 1e-3f);
        TT_ASSERT(allclose);
    }

    tt::tt_metal::program_cache::enable();

    auto run_binary_ops = [&] {
        {
            auto allclose = run_test<host_function<std::plus<float>>>(tt::tt_metal::add, device);
            TT_ASSERT(allclose);
        }

        {
            auto allclose = run_test<host_function<std::minus<float>>>(tt::tt_metal::sub, device);
            TT_ASSERT(allclose);
        }

        {
            auto allclose =
                run_test<host_function<std::multiplies<float>>>(tt::tt_metal::mul, device, 1e-2f, 1e-3f);
            TT_ASSERT(allclose);
        }
    };

    run_binary_ops();
    run_binary_ops();

    // Allocate a tensor to show that the addresses aren't cached
    auto input_tensor =
        tt::numpy::random::uniform(bfloat16(0.0f), bfloat16(0.0f), {1, 1, 32, 32}).to(Layout::TILE).to(device);

    run_binary_ops();

    TT_ASSERT(tt::tt_metal::program_cache::num_entries() == 3);

    tt::tt_metal::program_cache::disable_and_clear();

    TT_ASSERT(tt::tt_metal::program_cache::num_entries() == 0);

    TT_ASSERT(tt::tt_metal::CloseDevice(device));

    return 0;
}