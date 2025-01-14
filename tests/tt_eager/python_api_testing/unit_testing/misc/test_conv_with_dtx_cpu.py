# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import pytest


import numpy as np

import tt_lib as ttl
from tt_lib.utils import (
    blocked_mm_with_conv_act,
    tilize_to_list,
    tilize,
    untilize,
    _nearest_32,
    _nearest_y,
    convert_weights_2d_matrix,
)
from models.utility_functions import print_diff_argmax, is_close, comp_pcc
from tests.tt_eager.python_api_testing.conv.pytorch_conv_tb import (
    TestLevel,
    generate_conv_tb_with_pytorch_golden,
    generate_conv_tb,
)
from tests.tt_eager.python_api_testing.conv.conv_unit_test_utils import (
    create_conv_act_tensor,
    create_conv_weight_tensor,
)

import torch


@pytest.mark.parametrize(
    "K, C, H, W, R, S, stride_h, stride_w, pad_h, pad_w",
    (
        # channels padding
        (32, 3, 5, 5, 1, 1, 1, 1, 0, 0),
        # Hat = 1, Wat = 1, Wbt = 1
        (32, 32, 5, 5, 1, 1, 1, 1, 0, 0),
        # Hat = 2, Wat = 1, Wbt = 1
        (32, 32, 8, 8, 1, 1, 1, 1, 0, 0),
        # Hat = 1, Wat = 2, Wbt = 1
        (32, 64, 5, 5, 1, 1, 1, 1, 0, 0),
        # Hat = 2, Wat = 2, Wbt = 1
        (32, 64, 8, 8, 1, 1, 1, 1, 0, 0),
        # Hat = 1, Wat = 1, Wbt = 2
        (64, 32, 5, 5, 1, 1, 1, 1, 0, 0),
        # Hat = 1, Wat = 2, Wbt = 2
        (64, 64, 5, 5, 1, 1, 1, 1, 0, 0),
        # Hat = 2, Wat = 1, Wbt = 2
        (64, 32, 8, 8, 1, 1, 1, 1, 0, 0),
        # Hat = 2, Wat = 2, Wbt = 2
        (64, 64, 8, 8, 1, 1, 1, 1, 0, 0),
    ),
)
def test_run_conv_as_large_matmul_cpu(K, C, H, W, R, S, stride_h, stride_w, pad_h, pad_w):
    a_activation_shape = [1, C, H, W]
    A_pyt = torch.randn(a_activation_shape, dtype=torch.bfloat16).float()
    b_weights_shape = [K, C, R, S]
    B_pyt = torch.randn(b_weights_shape, dtype=torch.bfloat16).float()

    # Parameters defining block dims
    act_block_h = 4
    act_block_w = 4
    weight_block_h = act_block_w
    weight_block_w = 4
    OH = ((int)((H - R + 2 * pad_h) / stride_h)) + 1
    OW = ((int)((W - S + 2 * pad_w) / stride_w)) + 1
    mm_output_shape = [
        1,
        1,
        _nearest_y(OH * OW, 32 * act_block_h),
        _nearest_y(K, 32 * weight_block_w),
    ]

    # Prepare activations
    A_cl = create_conv_act_tensor(A_pyt, 1, C, H, W)
    # Prepare weights
    B_tiled = create_conv_weight_tensor(B_pyt, K, C, R, S, weight_block_h, weight_block_w)

    # Call DTX pass to transform A
    act_block_width_datums = act_block_w * 32
    act_block_height_datums = act_block_h * 32
    weight_block_width_datums = weight_block_w * 32
    matrix_activation_h_tiles = (int)(_nearest_y(OH * OW, act_block_height_datums) / 32)
    matrix_weight_w_tiles = (int)(_nearest_y(K, weight_block_width_datums) / 32)
    matrix_activation_w_tiles = (int)(_nearest_y(_nearest_y(C, 16) * R * S, act_block_width_datums) / 32)

    num_blocks_act_w = (int)(matrix_activation_w_tiles / act_block_w)
    num_blocks_act_h = (int)(matrix_activation_h_tiles / act_block_h)
    num_blocks_weight_w = (int)(matrix_weight_w_tiles / weight_block_w)
    (act_address_map, weight_address_map) = ttl.dtx.conv_transform(
        [_nearest_y(C, 16), H, W],
        [_nearest_y(K, weight_block_width_datums), _nearest_y(C, 16), R, S],
        [R, S, stride_h, stride_w, pad_h, pad_w],
        act_block_height_datums,
        act_block_width_datums,
        weight_block_width_datums,
        num_blocks_act_h,
        num_blocks_weight_w,
        1,
        False,
    )

    # Run host side CPU function
    out_pytorch = blocked_mm_with_conv_act(
        A_cl.buffer(),
        B_tiled.buffer(),
        act_address_map,
        weight_address_map,
        num_blocks_act_h,
        num_blocks_act_w,
        num_blocks_weight_w,
        act_block_h,
        act_block_w,
        weight_block_w,
    )
    assert list(out_pytorch.shape) == mm_output_shape
    out_pytorch = out_pytorch[:, :, 0 : (OH * OW), 0:K]

    # Convert matmul output layout to conv output layout
    out_tr = torch.transpose(out_pytorch, 2, 3)
    assert list(out_tr.shape) == [1, 1, K, (OH * OW)]
    out_result = out_tr.reshape([1, K, OH, OW])

    # Calculate conv result with golden result. Run Pytorch conv
    out_golden = torch.nn.functional.conv2d(A_pyt, B_pyt, stride=(stride_h, stride_w), padding=(pad_h, pad_w))
    assert out_result.shape == out_golden.shape
    passing_pcc, output_pcc = comp_pcc(out_golden, out_result, 0.99)
    print("Passing=", passing_pcc)
    print("Output pcc=", output_pcc)
    assert passing_pcc
