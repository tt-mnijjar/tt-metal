// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_lib_bindings.hpp"
#include "tt_dnn/op_library/bmm/bmm_op.hpp"
#include "tt_dnn/op_library/nlp_tms/nlp_tms.hpp"

namespace tt::tt_metal::detail
{
    void TensorModuleCustomAndBMMOPs( py::module & m_tensor)
    {
        // matrix multiplication
        m_tensor.def("bmm_tilize_untilize", &bmm_tilize_untilize, R"doc(
            Perform a batched matmul ``A x B`` with two tensors, where batch and channel dims match.
            This op also supports tiling tensor A and untiling the output.

            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------|
            | Argument                      | Description                                           | Data type | Valid range | Required |
            +===============================+=======================================================+===========+=============+==========+
            | a                             | LHS matmul operand                                    | Tensor    |             | Yes      |
            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------+
            | b                             | RHS matmul operand                                    | Tensor    |             | Yes      |
            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------+
            | a_height_nblocks              | Number of blocks along A's height                     | uint32_t  |             | Yes      |
            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------+
            | a_width_nblocks               | Number of blocks along A's width (= along B's height) | uint32_t  |             | Yes      |
            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------+
            | b_width_nblocks               | Number of blocks along B's width                      | uint32_t  |             | Yes      |
            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------+
            | a_block_height_ntiles         | Number of tiles along height of an A block            | uint32_t  |             | Yes      |
            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------+
            | a_block_width_ntiles          | Number of tiles along width of an A block             | uint32_t  |             | Yes      |
            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------+
            | b_block_width_ntiles          | Number of tiles along width of a B block              | uint32_t  |             | Yes      |
            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------+
            | out_subblock_height_ntiles    | Height of subblocks on height for output              | uint32_t  |             | Yes      |
            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------+
            | out_subblock_width_ntiles     | Number of subblocks on width for output               | uint32_t  |             | Yes      |
            +-------------------------------+-------------------------------------------------------+-----------+-------------+----------+
        )doc");
        // *** matrix multiplication ***
        m_tensor.def("matmul", &matmul,
            py::arg("input").noconvert(), py::arg("other").noconvert(), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Perform a non-batched matrix multiplication ``arg0 x arg1`` with two tensors.

            Both input tensors must have BFLOAT16 data type.

            Output tensor will have BFLOAT16 data type.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "First tensor to multiply", "Tensor", "Tensor of shape [1, 1, Y, S]", "Yes"
                "other", "Second tensor to multiply", "Tensor", "Tensor of shape [1, 1, S, X]", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("bmm", &bmm,
            py::arg("input").noconvert(), py::arg("other").noconvert(), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Perform a batched matmul ``arg0 x arg1`` with two tensors, where batch dims match.

            Both input tensors must have BFLOAT16 data type.

            Output tensor will have BFLOAT16 data type.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "First tensor to multiply", "Tensor", "Tensor of shape [W, Z, Y, S]", "Yes"
                "other", "Second tensor to multiply", "Tensor", "Tensor of shape [W, Z, S, X]", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        // Custom BERT matmuls/bmms
        m_tensor.def("bert_large_fused_qkv_matmul", &bert_large_fused_qkv_matmul,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("bias").noconvert() = std::nullopt, py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a bert_large_fused_qkv non-batched matmul ``A x B`` with two tensors.
        )doc");
        m_tensor.def("bert_large_ff1_matmul", &bert_large_ff1_matmul,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("bias").noconvert() = std::nullopt, py::arg("fused_activation") = std::nullopt, py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a bert_large_ff1 non-batched matmul ``A x B`` with two tensors.
        )doc");
        m_tensor.def("bert_large_ff2_matmul", &bert_large_ff2_matmul,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("bias").noconvert() = std::nullopt, py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a bert_large_ff2 non-batched matmul ``A x B`` with two tensors.
        )doc");
        m_tensor.def("bert_large_selfout_matmul", &bert_large_selfout_matmul,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("bias").noconvert() = std::nullopt, py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a bert_large_selfout non-batched matmul ``A x B`` with two tensors.
        )doc");
        m_tensor.def("bert_large_pre_softmax_bmm", &bert_large_pre_softmax_bmm,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a bert_large_pre_softmax_bmm batched matmul ``[9, 16, 384, 64] x [9, 16, 64, 384]`` with two tensors and returns a reshaped output of [9, 1, 6144, 384].
        )doc");
        m_tensor.def("bert_large_post_softmax_bmm", &bert_large_post_softmax_bmm,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a bert_large_post_softmax_bmm batched matmul by reshaping tensor A to [9, 16, 384, 384] first, then returning ``[9, 16, 384, 384] x [9, 16, 384, 64]``.
        )doc");

        // Custom Falcon matmuls/bmms
        m_tensor.def("falcon_fused_qkv_matmul", &falcon_fused_qkv_matmul,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("bias").noconvert() = std::nullopt, py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a falcon_fused_qkv non-batched matmul ``A x B`` with two tensors.
        )doc");
        m_tensor.def("falcon_selfout_matmul", &falcon_selfout_matmul,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("bias").noconvert() = std::nullopt, py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a falcon_selfout non-batched matmul ``A x B`` with two tensors.
        )doc");
        m_tensor.def("falcon_dense_4h_to_h_matmul", &falcon_dense_4h_to_h_matmul,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("bias").noconvert() = std::nullopt, py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a falcon_dense_4h_to_h non-batched matmul ``A x B`` with two tensors.
        )doc");
        m_tensor.def("falcon_dense_h_to_4h_matmul", &falcon_dense_h_to_4h_matmul,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("bias").noconvert() = std::nullopt, py::arg("fused_activation") = std::nullopt, py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a falcon_dense_h_to_4h non-batched matmul ``A x B`` with two tensors. This invokes the MULTI_CORE matmul parallelization. This parallelization does not support bias option yet.
        )doc");
        m_tensor.def("falcon_lm_head_matmul", &falcon_lm_head_matmul,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("bias").noconvert() = std::nullopt, py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, py::arg("output_dtype").noconvert() = std::nullopt, R"doc(
            Perform a falcon_lm_head non-batched matmul ``A x B`` with two tensors. This invokes the MULTI_CORE matmul parallelization. This parallelization does not support bias option yet.
        )doc");

        // Custom Generic NLP TMs
        // TODO: Uplift nlp_create_qkv_heads to support generic qkv num_heads and head_dim
        // This op should support arbitrary B and S divisible by 32 on DRAM; on L1, might error out due to space
        m_tensor.def("nlp_create_qkv_heads", &nlp_create_qkv_heads,
            py::arg().noconvert(), py::arg("output_mem_config") = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Shuffles [B, 1, S, 4672] fused qkv matrix into 3 heads with shapes [B, 71, S, 64], [B, 1, S, 64], and [B, 1, S, 64].
        )doc");
        // TODO: Uplift nlp_concat_heads to support generic num_heads and head_dim
        // This op should support arbitrary B and S divisible by 32 on DRAM; on L1, might error out due to space
        m_tensor.def("nlp_concat_heads", &nlp_concat_heads,
            py::arg().noconvert(), py::arg("output_mem_config") = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Shuffles [B, 71, S, 64] tensor into tensor with shape [B, 1, S, 4544].
        )doc");
    }

}