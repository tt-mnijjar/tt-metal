#include "tensor/tensor.hpp"

#include "tensor/tensor_impl.hpp"
#include "tensor/tensor_impl_wrapper.hpp"
#include "tensor/tensor_utils.hpp"
#include "common/bfloat16.hpp"
#include "llrt/llrt.hpp"
#include "tt_metal/common/constants.hpp"
#include "tt_metal/common/math.hpp"

#include "tt_stl/reflection.hpp"

#include "third_party/magic_enum/magic_enum.hpp"
#include "tt_metal/third_party/tracy/public/tracy/Tracy.hpp"



using namespace tt::constants;

namespace tt {

namespace tt_metal {

Tensor::Tensor(const Storage& storage, const Shape& shape, DataType dtype, Layout layout)
    : storage_(storage), shape_(shape), dtype_(dtype), layout_(layout) {
    std::visit(
        [&] (auto&& storage) {
            using StorageType = std::decay_t<decltype(storage)>;
            if constexpr (std::is_same_v<StorageType, OwnedStorage>) {
                TT_ASSERT(this->shape_.rank() == 4);
            }
            else if constexpr (std::is_same_v<StorageType, DeviceStorage>) {
                TT_ASSERT(this->shape_.rank() == 4);
                TT_ASSERT(storage.device != nullptr);
                tensor_impl::validate_on_device_dtype_and_layout(storage.device, dtype, layout);
            }
            else if constexpr (std::is_same_v<StorageType, BorrowedStorage>) {
                // do nothing
            }
            else {
                raise_unsupported_storage<StorageType>();
            }
        },
        this->storage_
    );
}

Tensor::~Tensor() {
    this->deallocate();
}

void Tensor::deallocate() {
    ZoneScoped;

    std::visit(
        [](auto&& storage)
        {
            using T = std::decay_t<decltype(storage)>;
            if constexpr (std::is_same_v<T, OwnedStorage>) {
                std::visit([](auto&& buffer) { buffer.reset(); }, storage.buffer);
            }
            else if constexpr (std::is_same_v<T, DeviceStorage>) {
                if (storage.buffer.use_count() == 1) {
                     DeallocateBuffer(*storage.buffer);
                }
                storage.buffer.reset();
            }
            else if constexpr (std::is_same_v<T, BorrowedStorage>) {
                // do nothing
            }
            else {
                raise_unsupported_storage<T>();
            }
        },
        this->storage_
    );
}

Tensor Tensor::to(Device *target_device, const MemoryConfig &mem_config) const {
    ZoneScoped;

    if (storage_type() == StorageType::DEVICE) {
        TT_ASSERT(this->device() == target_device && "Currently do not support moving between devices");
        return *this;
    }
    tensor_impl::validate_on_device_dtype_and_layout(target_device, this->dtype(), this->layout());
    return tensor_impl::to_device_wrapper(*this, target_device, mem_config);
}

Tensor Tensor::cpu() const {
    ZoneScoped;
    if (storage_type() == StorageType::OWNED) {
        return *this;
    }
    return tensor_impl::to_host_wrapper(*this);
}

Tensor Tensor::to(Layout target_layout) const {
    ZoneScoped;
    TT_ASSERT(this->storage_type() != StorageType::DEVICE && "Bring tensor to host before converting to target layout");
    return tensor_impl::to_layout_wrapper(*this, target_layout);
}

void Tensor::print(Layout print_layout, bool pretty_print) const {
    tensor_impl::print_wrapper(*this, print_layout, pretty_print);
}

Tensor Tensor::pad(const Shape &output_tensor_shape, const Shape &input_tensor_start, float pad_value) const {
    ZoneScoped;
    TT_ASSERT(this->storage_type() == StorageType::OWNED or this->storage_type() == StorageType::BORROWED && "Tensor must be on host for padding");
    TT_ASSERT(this->layout() == Layout::ROW_MAJOR && "Tensor layout must be ROW_MAJOR for padding");
    return tensor_impl::pad_wrapper(*this, output_tensor_shape, input_tensor_start, pad_value);
}

Tensor Tensor::unpad(const Shape &output_tensor_start, const Shape &output_tensor_end) const {
    ZoneScoped;
    TT_ASSERT(this->storage_type() == StorageType::OWNED && "Tensor must be on host for unpadding");
    TT_ASSERT(this->layout() == Layout::ROW_MAJOR && "Tensor layout must be ROW_MAJOR for unpadding");
    return tensor_impl::unpad_wrapper(*this, output_tensor_start, output_tensor_end);
}

Tensor Tensor::pad_to_tile(float pad_value) const {
    ZoneScoped;
    uint32_t h = this->shape()[2];
    uint32_t w = this->shape()[3];
    uint32_t padded_h = round_up(h, TILE_HEIGHT);
    uint32_t padded_w = round_up(w, TILE_WIDTH);

    auto padding = Padding({{0, 0}, {0, 0}, {0, padded_h - h}, {0, padded_w - w}}, Padding::PadValue::Any);

    Shape output_tensor_shape = Shape({this->shape()[0], this->shape()[1], padded_h, padded_w}, padding);
    Shape input_tensor_start = {0, 0, 0, 0};

    return this->pad(output_tensor_shape, input_tensor_start, pad_value);
}

Tensor Tensor::unpad_from_tile(const Shape &output_tensor_shape) const {
    ZoneScoped;

    TT_ASSERT(this->shape()[0] == output_tensor_shape[0] && this->shape()[1] == output_tensor_shape[1], "Input shape must match output shape apart from last 2 dims");
    TT_ASSERT(this->shape()[2] % TILE_HEIGHT == 0 && this->shape()[3] % TILE_WIDTH==0, "Last 2 dims of input shape must be multiples of 32");
    TT_ASSERT(this->shape()[2] - TILE_HEIGHT < output_tensor_shape[2] && this->shape()[3] - TILE_WIDTH < output_tensor_shape[3], "Last 2 dims of output must be within range to have been padded to input");
    Shape output_tensor_start = {0, 0, 0, 0};
    Shape output_tensor_end = {output_tensor_shape[0] - 1, output_tensor_shape[1] - 1, output_tensor_shape[2] - 1, output_tensor_shape[3] - 1};
    return this->unpad(output_tensor_start, output_tensor_end);
}

// Prints like numpy print function to make it more readable. Only supports row major layout.
void Tensor::pretty_print() const {
    print(Layout::ROW_MAJOR, /*pretty_print=*/true);
}

uint32_t Tensor::element_size() const {
    return tensor_impl::element_size_bytes_wrapper(this->dtype_);
}

Tensor Tensor::reshape(int N, int C, int H, int W) {
    auto new_shape = infer_dims_for_reshape(N, C, H, W, this->volume());
    return this->reshape(new_shape);
}

Tensor Tensor::reshape(const Shape& new_shape) const {
    TT_ASSERT(this->volume() == tt::tt_metal::compute_volume(new_shape));
    if (this->layout() == Layout::TILE) {
        TT_ASSERT(new_shape[2] % TILE_HEIGHT == 0 && new_shape[3] % TILE_WIDTH == 0 && "Expected a multiple of 32 for H, W (or -1 evaluating to such) in Tensor::reshape()!");
    }

    auto new_tensor = *this;
    new_tensor.shape_ = new_shape;
    return new_tensor;
}

bool Tensor::is_allocated() const {
    return std::visit(
        [](auto&& storage) -> bool
        {
            using T = std::decay_t<decltype(storage)>;
            if constexpr (std::is_same_v<T, OwnedStorage>) {
                return std::visit([](auto&& buffer) -> bool { return buffer.is_allocated(); }, storage.buffer);
            }
            else if constexpr (std::is_same_v<T, DeviceStorage>) {
                return bool(storage.buffer);
            }
            else if constexpr (std::is_same_v<T, BorrowedStorage>) {
                return true;
            }
            else {
                raise_unsupported_storage<T>();
            }
        },
        this->storage_
    );
}


StorageType Tensor::storage_type() const {
    return std::visit(
        [] (auto&& storage) -> StorageType
        {
            using T = std::decay_t<decltype(storage)>;
            if constexpr (std::is_same_v<T, OwnedStorage>) {
                return StorageType::OWNED;
            }
            else if constexpr (std::is_same_v<T, DeviceStorage>) {
                return StorageType::DEVICE;
            }
            else if constexpr (std::is_same_v<T, BorrowedStorage>) {
                return StorageType::BORROWED;
            }
            else {
                raise_unsupported_storage<T>();
            }
        },
        this->storage_
    );
}

const Storage& Tensor::storage() const {
    return this->storage_;
}

namespace detail {
const Shape compute_strides(const Shape& shape) {
    auto num_elements = compute_volume(shape);
    std::vector<std::uint32_t> strides;
    for (std::int32_t index = 0; index < shape.rank(); index++) {
        num_elements /= shape[index];
        strides.push_back(num_elements);
    }
    return strides;
}
}

const Shape Tensor::strides() const {
    return detail::compute_strides(this->shape());
}

uint32_t Tensor::volume() const {
    return tt::tt_metal::compute_volume(this->shape_);
}

tt::stl::reflection::Attributes Tensor::attributes() const {
    return {
        {"storage", this->storage_},
        {"shape", this->shape_},
        {"dtype", this->dtype_},
        {"layout", this->layout_},
    };
}

Tensor create_device_tensor(const Shape& shape, DataType data_type, Layout layout, Device *device, const MemoryConfig& memory_config) {
    ZoneScoped;
    uint32_t packed_size_in_bytes = tensor_impl::packed_buffer_size_bytes_wrapper(data_type, compute_buffer_size(shape, data_type));
    auto device_buffer = tensor_impl::allocate_buffer_on_device(packed_size_in_bytes, device, shape, data_type, layout, memory_config);
    return Tensor(DeviceStorage{device_buffer, device, memory_config}, shape, data_type, layout);
}

}  // namespace tt_metal

}  // namespace tt