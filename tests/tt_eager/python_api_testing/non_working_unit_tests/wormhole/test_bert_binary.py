# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys
from loguru import logger
import numpy as np
import random
from itertools import product
import pytest
import torch
import tt_lib as ttl

from tests.tt_eager.python_api_testing.sweep_tests.comparison_funcs import comp_pcc
import tests.tt_eager.python_api_testing.sweep_tests.pytorch_ops as pytorch_ops
import tests.tt_eager.python_api_testing.sweep_tests.tt_lib_ops as tt_lib_ops


def run_binary_bert_tests(tt_op, pt_op, input_shapes, dtype, dlayout, in_mem_config, out_mem_config, data_seed, device):
    torch.manual_seed(data_seed)

    x = torch.Tensor(size=input_shapes[0]).uniform_(-100, 100)
    y = torch.Tensor(size=input_shapes[1]).uniform_(-100, 100)

    # get referent value
    ref_value = pt_op(x, y)

    # calculate tt output
    logger.info(f"Running {tt_op} test")
    tt_result = tt_op(
        x=x,
        y=y,
        device=device,
        dtype=dtype,
        layout=dlayout,
        input_mem_config=in_mem_config,
        output_mem_config=out_mem_config,
    )
    logger.info("Done")

    # compare tt and golden outputs
    success, pcc_value = comp_pcc(ref_value, tt_result)
    logger.debug(pcc_value)

    assert success


supported_binary_data_types = [
    [ttl.tensor.DataType.BFLOAT16, ttl.tensor.DataType.BFLOAT8_B],
    [ttl.tensor.DataType.BFLOAT16, ttl.tensor.DataType.BFLOAT8_B],
    [
        ttl.tensor.MemoryConfig(ttl.tensor.TensorMemoryLayout.INTERLEAVED, ttl.tensor.BufferType.DRAM),
        ttl.tensor.MemoryConfig(ttl.tensor.TensorMemoryLayout.INTERLEAVED, ttl.tensor.BufferType.L1),
    ],
    [
        ttl.tensor.MemoryConfig(ttl.tensor.TensorMemoryLayout.INTERLEAVED, ttl.tensor.BufferType.DRAM),
        ttl.tensor.MemoryConfig(ttl.tensor.TensorMemoryLayout.INTERLEAVED, ttl.tensor.BufferType.L1),
    ],
    [
        ttl.tensor.MemoryConfig(ttl.tensor.TensorMemoryLayout.INTERLEAVED, ttl.tensor.BufferType.DRAM),
        ttl.tensor.MemoryConfig(ttl.tensor.TensorMemoryLayout.INTERLEAVED, ttl.tensor.BufferType.L1),
    ],
]

test_sweep_args_binary = []

for dtype_0, dtype_1, mem_cfg_0, mem_cfg_1, out_mem_cfg in product(*supported_binary_data_types):
    test_sweep_args_binary.append(
        (
            tt_lib_ops.bert_large_pre_softmax_bmm,
            pytorch_ops.bert_large_pre_softmax_bmm,
            [(9, 16, 384, 64), (9, 16, 64, 384)],
            [dtype_0, dtype_1],
            [ttl.tensor.Layout.TILE, ttl.tensor.Layout.TILE],
            [mem_cfg_0, mem_cfg_1],
            out_mem_cfg,
            random.randint(0, 20000000),
        )
    )

    test_sweep_args_binary.append(
        (
            tt_lib_ops.bert_large_post_softmax_bmm,
            pytorch_ops.bert_large_post_softmax_bmm,
            [(9, 16, 384, 384), (9, 16, 384, 64)],
            [dtype_0, dtype_1],
            [ttl.tensor.Layout.TILE, ttl.tensor.Layout.TILE],
            [mem_cfg_0, mem_cfg_1],
            out_mem_cfg,
            random.randint(0, 20000000),
        )
    )


@pytest.mark.parametrize(
    "tt_op, pt_op, input_shapes, dtype, dlayout, in_mem_config, out_mem_config, data_seed",
    (test_sweep_args_binary),
)
def test_binary_bert_tests(
    tt_op, pt_op, input_shapes, dtype, dlayout, in_mem_config, out_mem_config, data_seed, device
):
    random.seed(0)
    run_binary_bert_tests(tt_op, pt_op, input_shapes, dtype, dlayout, in_mem_config, out_mem_config, data_seed, device)