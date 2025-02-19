# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

from loguru import logger

import torch
import pytest
from models.utility_functions import skip_for_wormhole_b0
from tests.ttnn.utils_for_testing import assert_with_pcc
import ttnn
import math
from tests.ttnn.unit_tests.operations.test_conv2d import run_conv

# from test_conv import run_conv


@skip_for_wormhole_b0()
@pytest.mark.parametrize(
    "batch_size, output_channels, input_channels, input_height, input_width, filter_height, filter_width, stride_h, stride_w, pad_h, pad_w, bias, use_1d_systolic_array, config_override",
    (
        # (2, 64, 3, 300, 300, 7, 7, 2, 2, 3, 3, 0, True, None),
        (2, 64, 64, 75, 75, 3, 3, 1, 1, 1, 1, 0, True, None),
        (2, 128, 64, 75, 75, 3, 3, 2, 2, 1, 1, 0, False, None),  # fixed config after disabling use_1d_systolic_array
        # (2, 128, 64, 75, 75, 1, 1, 2, 2, 0, 0, 0, True, None), #5186 and 1x1 conv not supported yet
        (2, 128, 128, 38, 38, 3, 3, 1, 1, 1, 1, 0, True, None),
        (2, 256, 128, 38, 38, 3, 3, 1, 1, 1, 1, 0, True, None),
        # (2, 256, 128, 38, 38, 1, 1, 1, 1, 0, 0, 0, True), #1x1 conv not supported yet
        (2, 256, 128, 10, 10, 3, 3, 2, 2, 1, 1, 1, True, None),
        # (2, 256, 128, 5, 5, 3, 3, 1, 1, 0, 0, 1, False, None), #5328
        # (2, 256, 128, 3, 3, 3, 3, 1, 1, 0, 0, 1, True, NOne), #5328
        (2, 256, 256, 38, 38, 3, 3, 1, 1, 1, 1, 0, True, None),
        # (2, 256, 256, 38, 38, 1, 1, 1, 1, 0, 0, 1, True), #1x1 conv not supported yet
        (2, 512, 256, 38, 38, 3, 3, 2, 2, 1, 1, 1, True, None),
        # (2, 512, 256, 19, 19, 3, 3, 2, 2, 1, 1, 1, True), #5329
        # (2, 128, 256, 5, 5, 1, 1, 1, 1, 0, 0, 1, True), #1x1 conv not supported yet
        # (2, 128, 256, 3, 3, 1, 1, 1, 1, 0, 0, 1, True), #5328
        (2, 16, 256, 38, 38, 3, 3, 1, 1, 1, 1, 1, True, None),
        (2, 324, 256, 38, 38, 3, 3, 1, 1, 1, 1, 1, True, None),
        (2, 24, 256, 5, 5, 3, 3, 1, 1, 1, 1, 1, True, None),
        (2, 486, 256, 5, 5, 3, 3, 1, 1, 1, 1, 1, True, None),
        # (2, 16, 256, 3, 3, 3, 3, 1, 1, 1, 1, 1, True), #5328
        # (2, 324, 256, 3, 3, 3, 3, 1, 1, 1, 1, 1, True), #5328
        # (2, 16, 256, 1, 1, 3, 3, 1, 1, 1, 1, 1, True), #5328
        # (2, 324, 256, 1, 1, 3, 3, 1, 1, 1, 1, 1, True), #5328
        # (2, 256, 512, 19, 19, 1, 1, 1, 1, 0, 0, 1, True), #1x1 conv not supported yet
        # (2, 128, 512, 10, 10, 1, 1, 1, 1, 0, 0, 1, True),  #1x1 conv not supported yet
        (2, 24, 512, 19, 19, 3, 3, 1, 1, 1, 1, 1, True, None),
        (
            2,
            486,
            512,
            19,
            19,
            3,
            3,
            1,
            1,
            1,
            1,
            1,
            False,
            None,
        ),  # 5368 fixed config after disabling use_1d_systolic_array
        (2, 24, 512, 10, 10, 3, 3, 1, 1, 1, 1, 1, True, None),
        (
            2,
            486,
            512,
            10,
            10,
            3,
            3,
            1,
            1,
            1,
            1,
            1,
            False,
            None,
        ),  # 5368 fixed config after disabling use_1d_systolic_array
        (2, 64, 3, 224, 224, 7, 7, 2, 2, 3, 3, 0, True, None),
        # (2, 64, 64, 56, 56, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 64, 64, 56, 56, 3, 3, 1, 1, 1, 1, 0, True, None),
        # (2, 256, 64, 56, 56, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 64, 256, 56, 56, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 128, 256, 56, 56, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 128, 128, 56, 56, 3, 3, 2, 2, 1, 1, 0, True, None),
        # (2, 512, 128, 28, 28, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 512, 256, 56, 56, 1, 1, 2, 2, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 128, 512, 28, 28, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 128, 128, 28, 28, 3, 3, 1, 1, 1, 1, 0, True, None),
        # (2, 256, 512, 28, 28, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 256, 256, 28, 28, 3, 3, 2, 2, 1, 1, 0, True, None),
        # (2, 1024, 256, 14, 14, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 1024, 512, 28, 28, 1, 1, 2, 2, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 256, 1024, 14, 14, 1, 1, 1, 1, 0, 0, 0, True, None),  #1x1 conv not supported yet
        (2, 256, 256, 14, 14, 3, 3, 1, 1, 1, 1, 0, True, None),
        # (2, 512, 1024, 14, 14, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (
            2,
            512,
            512,
            14,
            14,
            3,
            3,
            2,
            2,
            1,
            1,
            0,
            False,
            None,
        ),  # 5368 fixed config after disabling use_1d_systolic_array
        # (2, 2048, 512, 7, 7, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 2048, 1024, 14, 14, 1, 1, 2, 2, 0, 0, 0, True, None),
        # (2, 512, 2048, 7, 7, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 512, 512, 7, 7, 3, 3, 1, 1, 1, 1, 0, True, None), #5368
        # (2, 48, 3, 640, 640, 6, 6, 2, 2, 2, 2, 0, True, None), #5368
        # (2, 96, 48, 320, 320, 3, 3, 2, 2, 1, 1, 0, True, None), #5368
        # (2, 48, 96, 160, 160, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 48, 48, 160, 160, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 48, 48, 160, 160, 3, 3, 1, 1, 1, 1, 0, True, None),
        # (2, 96, 96, 160, 160, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 192, 96, 160, 160, 3, 3, 2, 2, 1, 1, 0, True, None),
        # (2, 96, 192, 80, 80, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 96, 96, 80, 80, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 96, 96, 80, 80, 3, 3, 1, 1, 1, 1, 0, True, None),
        # (2, 192, 192, 80, 80, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 384, 192, 80, 80, 3, 3, 2, 2, 1, 1, 0, True, None),
        # (2, 192, 384, 40, 40, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 192, 192, 40, 40, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 192, 192, 40, 40, 3, 3, 1, 1, 1, 1, 0, True, None),
        # (2, 384, 384, 40, 40, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 768, 384, 40, 40, 3, 3, 2, 2, 1, 1, 0, True, None), #5368
        # (2, 384, 768, 20, 20, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 384, 384, 20, 20, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 384, 384, 20, 20, 3, 3, 1, 1, 1, 1, 0, True, None),
        # (2, 768, 768, 20, 20, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 768, 1536, 20, 20, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 192, 768, 40, 40, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        # (2, 96, 384, 80, 80, 1, 1, 1, 1, 0, 0, 0, True, None), #1x1 conv not supported yet
        (2, 192, 192, 80, 80, 3, 3, 2, 2, 1, 1, 0, True, None),
        (2, 384, 384, 40, 40, 3, 3, 2, 2, 1, 1, 0, True, None),
        # (2, 255, 192, 80, 80, 1, 1, 1, 1, 0, 0, 1, True, None), #1x1 conv not supported yet
        # (2, 255, 384, 40, 40, 1, 1, 1, 1, 0, 0, 1, True, None), #1x1 conv not supported yet
        # (2, 255, 768, 20, 20, 1, 1, 1, 1, 0, 0, 1, True, None), #1x1 conv not supported yet
        # (2, 64, 3, 300, 300, 7, 7, 2, 2, 3, 3, 0, False, None),
        (2, 64, 64, 75, 75, 3, 3, 1, 1, 1, 1, 0, False, {"act_block_h": 32}),  # 5368 fixed after the added config
        (2, 128, 64, 75, 75, 3, 3, 2, 2, 1, 1, 0, False, None),
        # (2, 128, 64, 75, 75, 1, 1, 2, 2, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 128, 128, 38, 38, 3, 3, 1, 1, 1, 1, 0, False, None),
        (2, 256, 128, 38, 38, 3, 3, 1, 1, 1, 1, 0, False, None),
        # (2, 256, 128, 38, 38, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 256, 128, 10, 10, 3, 3, 2, 2, 1, 1, 1, False, None),
        # (2, 256, 128, 5, 5, 3, 3, 1, 1, 0, 0, 1, False, None), #5328
        # (2, 256, 128, 3, 3, 3, 3, 1, 1, 0, 0, 1, False, None), #5328
        (2, 256, 256, 38, 38, 3, 3, 1, 1, 1, 1, 0, False, None),
        # (2, 256, 256, 38, 38, 1, 1, 1, 1, 0, 0, 1, False, None), #1x1 conv not supported yet
        (2, 512, 256, 38, 38, 3, 3, 2, 2, 1, 1, 1, False, None),
        # (2, 512, 256, 19, 19, 3, 3, 2, 2, 1, 1, 1, False, None),
        # (2, 128, 256, 5, 5, 1, 1, 1, 1, 0, 0, 1, False, None), #1x1 conv not supported yet
        # (2, 128, 256, 3, 3, 1, 1, 1, 1, 0, 0, 1, False, None), #1x1 conv not supported yet
        # (2,16,256,38,38,3,3,1,1,1,1,1,False, None),
        # (2,324,256,38,38,3,3,1,1,1,1,1,False, None),
        # (2,24,256,5,5,3,3,1,1,1,1,1,False), #seg fault
        (2, 486, 256, 5, 5, 3, 3, 1, 1, 1, 1, 1, False, None),
        # (2, 16, 256, 3, 3, 3, 3, 1, 1, 1, 1, 1, False, None), #5328
        # (2, 324, 256, 3, 3, 3, 3, 1, 1, 1, 1, 1, False, None), #5328
        # (2, 16, 256, 1, 1, 3, 3, 1, 1, 1, 1, 1, False, None), #5328
        # (2, 324, 256, 1, 1, 3, 3, 1, 1, 1, 1, 1, False, None), #5328
        # (2, 256, 512, 19, 19, 1, 1, 1, 1, 0, 0, 1, False, None), #1x1 conv not supported yet
        # (2, 128, 512, 10, 10, 1, 1, 1, 1, 0, 0, 1, False, None), #1x1 conv not supported yet
        # (2,24,512,19,19,3,3,1,1,1,1,1,False),
        (2, 486, 512, 19, 19, 3, 3, 1, 1, 1, 1, 1, False, None),
        # (2,24,512,10,10,3,3,1,1,1,1,1,False),
        (2, 486, 512, 10, 10, 3, 3, 1, 1, 1, 1, 1, False, None),
        # (2,64,3,224,224,7,7,2,2,3,3,0,False),
        # (2, 64, 64, 56, 56, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 64, 64, 56, 56, 3, 3, 1, 1, 1, 1, 0, False, None),
        # (2, 256, 64, 56, 56, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 64, 256, 56, 56, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 128, 256, 56, 56, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 128, 128, 56, 56, 3, 3, 2, 2, 1, 1, 0, False, None),
        # (2, 512, 128, 28, 28, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 512, 256, 56, 56, 1, 1, 2, 2, 0, 0, 0, False, None), # 1x1 conv not supported yet
        # (2, 128, 512, 28, 28, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 128, 128, 28, 28, 3, 3, 1, 1, 1, 1, 0, False, None),
        # (2, 256, 512, 28, 28, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 256, 256, 28, 28, 3, 3, 2, 2, 1, 1, 0, False, None),
        # (2, 1024, 256, 14, 14, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 1024, 512, 28, 28, 1, 1, 2, 2, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 256, 1024, 14, 14, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 256, 256, 14, 14, 3, 3, 1, 1, 1, 1, 0, False, None),
        # (2, 512, 1024, 14, 14, 1, 1, 1, 1, 0, 0, 0, False, None),
        (2, 512, 512, 14, 14, 3, 3, 2, 2, 1, 1, 0, False, None),
        # (2, 2048, 512, 7, 7, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 2048, 1024, 14, 14, 1, 1, 2, 2, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 512, 2048, 7, 7, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2,512,512,7,7,3,3,1,1,1,1,0,False),
        # (2,48,3,640,640,6,6,2,2,2,2,0,False),
        # (2, 96, 48, 320, 320, 3, 3, 2, 2, 1, 1, 0, False, None), #5368
        # (2, 48, 96, 160, 160, 1, 1, 1, 1, 0, 0, 0, False, None), #5368
        # (2, 48, 48, 160, 160, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 48, 48, 160, 160, 3, 3, 1, 1, 1, 1, 0, False, {"act_block_h": 32}),  # 5368 fixed after the added config
        # (2, 96, 96, 160, 160, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 192, 96, 160, 160, 3, 3, 2, 2, 1, 1, 0, False, None), #5328
        # (2, 96, 192, 80, 80, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 96, 96, 80, 80, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 96, 96, 80, 80, 3, 3, 1, 1, 1, 1, 0, False, None), #5368
        (2, 384, 192, 80, 80, 3, 3, 2, 2, 1, 1, 0, False, None),
        # (2, 192, 192, 80, 80, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 192, 384, 40, 40, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 192, 192, 40, 40, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 192, 192, 40, 40, 3, 3, 1, 1, 1, 1, 0, False, None),
        # (2, 384, 384, 40, 40, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 768, 384, 40, 40, 3, 3, 2, 2, 1, 1, 0, False, None),
        # (2, 384, 768, 20, 20, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 384, 384, 20, 20, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 384, 384, 20, 20, 3, 3, 1, 1, 1, 1, 0, False, None),
        # (2, 768, 768, 20, 20, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 768, 1536, 20, 20, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 192, 768, 40, 40, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        # (2, 96, 384, 80, 80, 1, 1, 1, 1, 0, 0, 0, False, None), #1x1 conv not supported yet
        (2, 192, 192, 80, 80, 3, 3, 2, 2, 1, 1, 0, False, None),
        (2, 384, 384, 40, 40, 3, 3, 2, 2, 1, 1, 0, False, None),
        # (2, 255, 192, 80, 80, 1, 1, 1, 1, 0, 0, 1, False, None), #1x1 conv not supported yet
        # (2, 255, 384, 40, 40, 1, 1, 1, 1, 0, 0, 1, False, None), #1x1 conv not supported yet
        # (2, 255, 768, 20, 20, 1, 1, 1, 1, 0, 0, 1, False, None), #1x1 conv not supported yet
    ),
)
@pytest.mark.parametrize(
    "weights_dtype",
    [ttnn.bfloat8_b],
)
@pytest.mark.parametrize(
    "activations_dtype",
    [ttnn.bfloat8_b],
)
@pytest.mark.parametrize("math_fidelity", [ttnn.MathFidelity.LoFi])
def test_moreh_conv(
    use_program_cache,
    device,
    math_fidelity,
    activations_dtype,
    weights_dtype,
    batch_size,
    output_channels,
    input_channels,
    input_height,
    input_width,
    filter_height,
    filter_width,
    stride_h,
    stride_w,
    pad_h,
    pad_w,
    bias,
    use_1d_systolic_array,
    config_override,
):
    use_shallow_conv_variant = False
    if input_channels == 3:
        # use shallow conv variant for first conv only
        use_shallow_conv_variant = True
    run_conv(
        device,
        math_fidelity,
        activations_dtype,
        weights_dtype,
        batch_size,
        output_channels,
        input_channels,
        input_height,
        input_width,
        filter_height,
        filter_width,
        stride_h,
        stride_w,
        pad_h,
        pad_w,
        use_1d_systolic_array,
        config_override,
        use_shallow_conv_variant=use_shallow_conv_variant,
    )
