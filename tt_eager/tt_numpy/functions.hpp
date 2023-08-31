#include <tensor/tensor.hpp>
#include <tensor/tensor_utils.hpp>
#include <tensor/owned_buffer.hpp>
#include <tensor/owned_buffer_functions.hpp>
#include <tensor/types.hpp>
#include <common/math.hpp>

#include <optional>
#include <random>

namespace tt {

namespace numpy {

using tt_metal::Tensor;
using tt_metal::MemoryConfig;
using tt_metal::DataType;
using tt_metal::Layout;
using tt_metal::Shape;
using tt_metal::Device;
using tt_metal::StorageType;
using tt_metal::OwnedStorage;
namespace detail
{

template<typename T>
constexpr static DataType get_data_type() {
        if constexpr (std::is_same_v<T, uint32_t>) {
        return DataType::UINT32;
    }
    else if constexpr (std::is_same_v<T, float>) {
        return DataType::FLOAT32;
    }
    else if constexpr (std::is_same_v<T, bfloat16>) {
        return DataType::BFLOAT16;
    }
    else {
        TT_THROW("Unsupported DataType!");
    }
}

template<typename T>
static Tensor full(const Shape& shape, T value, const Layout layout = Layout::ROW_MAJOR, Device * device = nullptr, const MemoryConfig& output_mem_config = MemoryConfig{.interleaved = true}) {
    constexpr DataType data_type = detail::get_data_type<T>();
    auto owned_buffer = tt_metal::owned_buffer::create<T>(tt_metal::compute_volume(shape));
    std::fill(std::begin(owned_buffer), std::end(owned_buffer), value);
    auto output = Tensor(OwnedStorage{owned_buffer}, shape, data_type, layout);
    if (device != nullptr) {
        output = output.to(device, output_mem_config);
    }
    return output;
}

} // namespace detail

template<typename T>
static Tensor full(const Shape& shape, const T value, const DataType data_type, const Layout layout = Layout::ROW_MAJOR, Device * device = nullptr, const MemoryConfig& output_mem_config = MemoryConfig{.interleaved = true}) {
    switch (data_type) {
        case DataType::UINT32: {
            return detail::full<uint32_t>(shape, uint32_t(value), layout, device, output_mem_config);
        }
        case DataType::FLOAT32: {
            return detail::full<float>(shape, float(value), layout, device, output_mem_config);
        }
        case DataType::BFLOAT16: {
            return detail::full<bfloat16>(shape, bfloat16(value), layout, device, output_mem_config);
        }
        default:
            TT_THROW("Unsupported DataType!");
    }
}

static Tensor zeros(const Shape& shape, const DataType data_type = DataType::BFLOAT16, const Layout layout = Layout::ROW_MAJOR, Device * device = nullptr, const MemoryConfig& output_mem_config = MemoryConfig{.interleaved = true}) {
    return full(shape, 0.0f, data_type, layout, device, output_mem_config);
}

static Tensor ones(const Shape& shape, const DataType data_type = DataType::BFLOAT16, const Layout layout = Layout::ROW_MAJOR, Device * device = nullptr, const MemoryConfig& output_mem_config = MemoryConfig{.interleaved = true}) {
    return full(shape, 1.0f, data_type, layout, device, output_mem_config);
}

template<typename T>
static Tensor full_like(const Tensor& input_tensor, const T value, std::optional<DataType> data_type = std::nullopt, std::optional<Layout> layout = std::nullopt, std::optional<MemoryConfig> output_mem_config = std::nullopt) {
    DataType data_type_to_use = input_tensor.dtype();
    if (data_type.has_value()) {
        data_type_to_use = data_type.value();
    }
    Layout layout_to_use = input_tensor.layout();
    if (layout.has_value()) {
        layout_to_use = layout.value();
    }
    if (input_tensor.storage_type() == StorageType::DEVICE) {
        MemoryConfig output_mem_config_to_use = input_tensor.memory_config();
        if (output_mem_config.has_value()) {
            output_mem_config_to_use = output_mem_config.value();
        }
        return full(input_tensor.shape(), value, data_type_to_use, layout_to_use, input_tensor.device(), output_mem_config_to_use);
    } else {
        return full(input_tensor.shape(), value, data_type_to_use, layout_to_use);
    }
}

static Tensor zeros_like(const Tensor& input_tensor, std::optional<DataType> data_type = std::nullopt, std::optional<Layout> layout = std::nullopt, std::optional<MemoryConfig> output_mem_config = std::nullopt) {
    return full_like(input_tensor, 0.0f, data_type, layout, output_mem_config);
}

static Tensor ones_like(const Tensor& input_tensor, std::optional<DataType> data_type = std::nullopt, std::optional<Layout> layout = std::nullopt, std::optional<MemoryConfig> output_mem_config = std::nullopt) {
    return full_like(input_tensor, 1.0f, data_type, layout, output_mem_config);
}

template<typename T>
static Tensor arange(int64_t start, int64_t stop, int64_t step, const Layout layout = Layout::ROW_MAJOR, Device * device = nullptr, const MemoryConfig& output_mem_config = MemoryConfig{.interleaved = true}) {
    constexpr DataType data_type = detail::get_data_type<T>();
    // Current implementation restrictions
    TT_ASSERT(step > 0, "Step must be greater than 0");
    TT_ASSERT(start < stop, "Start must be less than step");
    auto size = div_up((stop - start), step);
    auto owned_buffer  = tt_metal::owned_buffer::create<T>(size);

    auto index = 0;
    for (auto value = start; value < stop; value += step) {
        if constexpr (std::is_same_v<T, bfloat16>) {
         owned_buffer[index++] = static_cast<T>(static_cast<float>(value));
        } else {
         owned_buffer[index++] = static_cast<T>(value);
        }
    }
    auto output = Tensor(OwnedStorage{owned_buffer}, {1, 1, 1, static_cast<uint32_t>(size)}, data_type, layout);
    if (device != nullptr) {
        output = output.to(device, output_mem_config);
    }
    return output;
}

namespace random {

inline auto RANDOM_GENERATOR = std::mt19937(0);

static void seed(std::size_t seed) {
    RANDOM_GENERATOR = std::mt19937(seed);
}

template<typename T>
static Tensor uniform(T low, T high, const Shape& shape, const Layout layout = Layout::ROW_MAJOR) {
    constexpr DataType data_type = detail::get_data_type<T>();

    auto owned_buffer = tt_metal::owned_buffer::create<T>(tt_metal::compute_volume(shape));

    if constexpr (std::is_same_v<T, uint32_t> ) {
        auto rand_value = std::bind(std::uniform_int_distribution<T>(low, high), RANDOM_GENERATOR);
        for (auto index = 0; index < owned_buffer.size(); index++) {
            owned_buffer[index] = rand_value();
        }
    } else if constexpr (std::is_same_v<T, float>) {
        auto rand_value = std::bind(std::uniform_real_distribution<T>(low, high), RANDOM_GENERATOR);
        for (auto index = 0; index < owned_buffer.size(); index++) {
            owned_buffer[index] = rand_value();
        }
    } else if constexpr (std::is_same_v<T, bfloat16>) {
        auto rand_value = std::bind(std::uniform_real_distribution<float>(low.to_float(), high.to_float()), RANDOM_GENERATOR);
        for (auto index = 0; index < owned_buffer.size(); index++) {
            owned_buffer[index] = bfloat16(rand_value());
        }
    }

    return Tensor(OwnedStorage{owned_buffer}, shape, data_type, layout);
}

static Tensor random(const Shape& shape, const DataType data_type = DataType::BFLOAT16, const Layout layout = Layout::ROW_MAJOR) {
    switch (data_type)
    {
        case DataType::UINT32:
            return uniform(0u, 1u, shape, layout);
        case DataType::FLOAT32:
            return uniform(0.0f, 1.0f, shape, layout);
        case DataType::BFLOAT16:
            return uniform(bfloat16(0.0f), bfloat16(1.0f), shape, layout);
        default:
            TT_THROW("Unsupported DataType!");
    };
}

}

namespace detail {
static bool nearly_equal(float a, float b, float epsilon = 1e-5f, float abs_threshold = 1e-5f) {
  auto diff = std::abs(a-b);
  auto norm = std::min((std::abs(a) + std::abs(b)), std::numeric_limits<float>::max());
  auto result = diff < std::max(abs_threshold, epsilon * norm);
  if (not result) {
    tt::log_error(tt::LogTest, "{} != {}", a, b);
  }
  return result;
}

template<typename ... Args>
static bool nearly_equal(bfloat16 a, bfloat16 b, Args ...  args) {
  return nearly_equal(a.to_float(), b.to_float(), args...);
}
}

template<typename DataType, typename ... Args>
static bool allclose(const Tensor& tensor_a, const Tensor& tensor_b, Args ...  args) {

    if (tensor_a.shape() != tensor_b.shape()) {
        return false;
    }

    if (tensor_a.dtype() != tensor_b.dtype()) {
        return false;
    }

    auto tensor_a_buffer = tt_metal::owned_buffer::get_as<DataType>(tensor_a);
    auto tensor_b_buffer = tt_metal::owned_buffer::get_as<DataType>(tensor_b);

    for (int index = 0; index < tensor_a_buffer.size(); index++) {
        if (not detail::nearly_equal(tensor_a_buffer[index], tensor_b_buffer[index], args...)) {
            return false;
        }
    }
    return true;
}


}
}