#pragma once

#include "transformers/module.hpp"

#include "tt_dnn/op_library/bmm/bmm_op.hpp"
#include "tt_dnn/op_library/layernorm/layernorm_op.hpp"
#include "tt_dnn/op_library/softmax/softmax_op.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace tt {
namespace operations {
namespace primary {

void py_module(py::module& m_primary) {


    auto m_transformers = m_primary.def_submodule("transformers", "Primary transformers operations");
    transformers::py_module(m_transformers);

    py::class_<MatmulProgramConfig>(m_primary, "MatmulProgramConfig");

    py::class_<MatmulDefaultProgramConfig>(m_primary, "MatmulDefaultProgramConfig")
        .def(py::init<>());

    py::class_<MatmulMultiCoreReuseProgramConfig>(m_primary, "MatmulMultiCoreReuseProgramConfig")
        .def(
            py::init<>(
                [] (
                    std::tuple<std::size_t, std::size_t> compute_with_storage_grid_size,
                    std::size_t in0_block_w,
                    std::size_t out_subblock_h,
                    std::size_t out_subblock_w,
                    std::size_t per_core_M,
                    std::size_t per_core_N
                ) {

                    return MatmulMultiCoreReuseProgramConfig{
                        .compute_with_storage_grid_size={std::get<0>(compute_with_storage_grid_size), std::get<1>(compute_with_storage_grid_size)},
                        .in0_block_w=in0_block_w,
                        .out_subblock_h=out_subblock_h,
                        .out_subblock_w=out_subblock_w,
                        .per_core_M=per_core_M,
                        .per_core_N=per_core_N,
                    };

                }
            ),
            py::kw_only(),
            py::arg("compute_with_storage_grid_size").noconvert(),
            py::arg("in0_block_w").noconvert(),
            py::arg("out_subblock_h").noconvert(),
            py::arg("out_subblock_w").noconvert(),
            py::arg("per_core_M").noconvert(),
            py::arg("per_core_N").noconvert()
        );
    py::class_<MatmulMultiCoreReuseMultiCastProgramConfig>(m_primary, "MatmulMultiCoreReuseMultiCastProgramConfig")
        .def(
            py::init<>(
                [] (
                    std::tuple<std::size_t, std::size_t> compute_with_storage_grid_size,
                    std::size_t in0_block_w,
                    std::size_t out_subblock_h,
                    std::size_t out_subblock_w,
                    std::size_t per_core_M,
                    std::size_t per_core_N,
                    bool fuse_gelu_activation
                ) {

                    return MatmulMultiCoreReuseMultiCastProgramConfig{
                        .compute_with_storage_grid_size={std::get<0>(compute_with_storage_grid_size), std::get<1>(compute_with_storage_grid_size)},
                        .in0_block_w=in0_block_w,
                        .out_subblock_h=out_subblock_h,
                        .out_subblock_w=out_subblock_w,
                        .per_core_M=per_core_M,
                        .per_core_N=per_core_N,
                        .fuse_gelu_activation=fuse_gelu_activation,
                    };

                }
            ),
            py::kw_only(),
            py::arg("compute_with_storage_grid_size").noconvert(),
            py::arg("in0_block_w").noconvert(),
            py::arg("out_subblock_h").noconvert(),
            py::arg("out_subblock_w").noconvert(),
            py::arg("per_core_M").noconvert(),
            py::arg("per_core_N").noconvert(),
            py::arg("fuse_gelu_activation").noconvert()
        );


    // TODO(arakhmati): delete redundant matmul overrides by figuring out how to pass in MatmulProgramConfig (which is a std::variant)
    m_primary.def(
        "matmul",
        [](const Tensor& input_tensor_a, const Tensor& input_tensor_b, const MatmulDefaultProgramConfig& program_config, const MemoryConfig& out_mem_config, std::optional<DataType> output_dtype) {
            return matmul(input_tensor_a, input_tensor_b, program_config, out_mem_config, output_dtype);
        },
        py::arg("input_tensor_a").noconvert(),
        py::arg("input_tensor_b").noconvert(),
        py::kw_only(),
        py::arg("program_config").noconvert() = MatmulDefaultProgramConfig(),
        py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG,
        py::arg("output_dtype").noconvert() = std::nullopt,
        R"doc(
            Perform a matrix multiplication ``input_tensor_a x input_tensor_b``.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input_tensor_a",    "First tensor to multiply",                               "Tensor",                                     "Tensor of shape [B_a, C_a, M, K]",                               "Yes"
                "input_tensor_b",    "Second tensor to multiply",                              "Tensor",                                     "Tensor of shape [B_b, C_b, K, N]",                               "Yes"
                "program_config",    "",                                                       "MatmulDefaultProgramConfig",          "",                                                               "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig",                               "Default is interleaved in DRAM",                                 "No"
                "output_dtype",      "Output Data Type",                                       "DataType",                                   "By default it will be set to the data type of `input_tensor_a`", "No"
        )doc"
    );

    m_primary.def(
        "matmul",
        [](const Tensor& input_tensor_a, const Tensor& input_tensor_b, const MatmulMultiCoreReuseProgramConfig& program_config, const MemoryConfig& out_mem_config, std::optional<DataType> output_dtype) {
            return matmul(input_tensor_a, input_tensor_b, program_config, out_mem_config, output_dtype);
        },
        py::arg("input_tensor_a").noconvert(),
        py::arg("input_tensor_b").noconvert(),
        py::kw_only(),
        py::arg("program_config").noconvert(),
        py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG,
        py::arg("output_dtype").noconvert() = std::nullopt,
        R"doc(
            Perform a matrix multiplication ``input_tensor_a x input_tensor_b``.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input_tensor_a",    "First tensor to multiply",                               "Tensor",                                     "Tensor of shape [B_a, C_a, M, K]",                               "Yes"
                "input_tensor_b",    "Second tensor to multiply",                              "Tensor",                                     "Tensor of shape [B_b, C_b, K, N]",                               "Yes"
                "program_config",    "",                                                       "MatmulMultiCoreReuseProgramConfig",          "",                                                               "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig",                               "Default is interleaved in DRAM",                                 "No"
                "output_dtype",      "Output Data Type",                                       "DataType",                                   "By default it will be set to the data type of `input_tensor_a`", "No"
        )doc"
    );

    m_primary.def(
        "matmul",
        [](const Tensor& input_tensor_a, const Tensor& input_tensor_b, std::optional<const Tensor> bias, const MatmulDefaultProgramConfig& program_config, const MemoryConfig& out_mem_config, std::optional<DataType> output_dtype) {
            return matmul(input_tensor_a, input_tensor_b, bias, program_config, out_mem_config, output_dtype);
        },
        py::arg("input_tensor_a").noconvert(),
        py::arg("input_tensor_b").noconvert(),
        py::kw_only(),
        py::arg("bias").noconvert() = std::nullopt,
        py::arg("program_config").noconvert() = MatmulDefaultProgramConfig(),
        py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG,
        py::arg("output_dtype").noconvert() = std::nullopt,
        R"doc(
            Perform a matrix multiplication ``input_tensor_a x input_tensor_b``.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input_tensor_a",    "First tensor to multiply",                               "Tensor",                                     "Tensor of shape [B_a, C_a, M, K]",                               "Yes"
                "input_tensor_b",    "Second tensor to multiply",                              "Tensor",                                     "Tensor of shape [B_b, C_b, K, N]",                               "Yes"
                "bias",              "Bias to add",                                            "Tensor",                                     "Tensor of shape [1, 1, 1, N]",                                   "Yes"
                "program_config",    "",                                                       "MatmulDefaultProgramConfig", "",                                                               "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig",                               "Default is interleaved in DRAM",                                 "No"
                "output_dtype",      "Output Data Type",                                       "DataType",                                   "By default it will be set to the data type of `input_tensor_a`", "No"
        )doc"
    );

    m_primary.def(
        "matmul",
        [](const Tensor& input_tensor_a, const Tensor& input_tensor_b, std::optional<const Tensor> bias, const MatmulMultiCoreReuseMultiCastProgramConfig& program_config, const MemoryConfig& out_mem_config, std::optional<DataType> output_dtype) {
            return matmul(input_tensor_a, input_tensor_b, bias, program_config, out_mem_config, output_dtype);
        },
        py::arg("input_tensor_a").noconvert(),
        py::arg("input_tensor_b").noconvert(),
        py::kw_only(),
        py::arg("bias").noconvert() = std::nullopt,
        py::arg("program_config").noconvert(),
        py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG,
        py::arg("output_dtype").noconvert() = std::nullopt,
        R"doc(
            Perform a matrix multiplication ``input_tensor_a x input_tensor_b``.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input_tensor_a",    "First tensor to multiply",                               "Tensor",                                     "Tensor of shape [B_a, C_a, M, K]",                               "Yes"
                "input_tensor_b",    "Second tensor to multiply",                              "Tensor",                                     "Tensor of shape [B_b, C_b, K, N]",                               "Yes"
                "bias",              "Bias to add",                                            "Tensor",                                     "Tensor of shape [1, 1, 1, N]",                                   "Yes"
                "program_config",    "",                                                       "MatmulMultiCoreReuseMultiCastProgramConfig", "",                                                               "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig",                               "Default is interleaved in DRAM",                                 "No"
                "output_dtype",      "Output Data Type",                                       "DataType",                                   "By default it will be set to the data type of `input_tensor_a`", "No"
        )doc"
    );


    m_primary.def(
        "layernorm",
        &layernorm,
        py::arg("input").noconvert(),
        py::arg("eps").noconvert(),
        py::arg("gamma").noconvert() = std::nullopt,
        py::arg("beta").noconvert() = std::nullopt,
        py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG,
        R"doc(
            Performs a layernorm operation on the last tensor dimension with optional fused with post-multiplication and addition via W-bcast.
        )doc"
    );

    m_primary.def(
        "add_layernorm",
        &add_layernorm,
        py::arg("a").noconvert(),
        py::arg("b").noconvert(),
        py::arg("eps").noconvert(),
        py::arg("gamma").noconvert() = std::nullopt,
        py::arg("beta").noconvert() = std::nullopt,
        py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG,
        R"doc(
            Performs a layernorm(a+b)*gamma + beta operation.
        )doc"
    );

    // softmax
    m_primary.def("softmax_in_place", &softmax_in_place,
        "Performs a softmax operation on the last tensor dimension. Returns a reference to the input tensor modified in place.");

}

}  // namespace primary
}  // namespace operations
}  // namespace tt