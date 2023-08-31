#pragma once

#include "tt_metal/host_api.hpp"
#include "tensor/tensor.hpp"


namespace tt {
namespace tt_metal {

enum class PoolType {
    AVG = 0
};

Tensor average_pool_2d(const Tensor& input);

}  // namespace tt_metal
}  // namespace tt