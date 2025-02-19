# SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import pytest
import torch

import tt_lib as ttl

from models.utility_functions import skip_for_wormhole_b0, torch2tt_tensor


def fold_torch(input_tensor, stride_h, stride_w):
    N, H, W, C = input_tensor.shape

    reshaped = input_tensor.reshape(N, H // stride_h, stride_h, W // stride_w, stride_w, C)
    transposed = reshaped.permute(0, 1, 3, 2, 4, 5)
    return transposed.reshape(N, H // stride_h, W // stride_w, C * stride_h * stride_w)


@skip_for_wormhole_b0()
@pytest.mark.parametrize(
    "act_shape,stride_h,stride_w",
    [
        ((1, 2, 2, 2), 2, 2),
        ((1, 2, 2, 16), 2, 2),
        ((10, 2, 2, 32), 2, 2),
        ((10, 4, 4, 32), 2, 2),
        ((10, 6, 8, 32), 3, 2),
        ((10, 6, 8, 32), 3, 1),
        ((10, 6, 8, 32), 1, 2),
        ((10, 6, 8, 32), 1, 1),
    ],
)
def test_fold(act_shape, stride_h, stride_w, device):
    torch.manual_seed(0)

    torch_input = torch.randn(act_shape, dtype=torch.bfloat16)
    expected = fold_torch(torch_input, stride_h, stride_w)

    tt_input = torch2tt_tensor(
        torch_input,
        device,
        ttl.tensor.Layout.ROW_MAJOR,
        ttl.tensor.MemoryConfig(ttl.tensor.TensorMemoryLayout.INTERLEAVED),
    )

    tt_out = ttl.tensor.fold(tt_input, stride_h, stride_w)
    tt_out = tt_out.cpu()
    actual = tt_out.to_torch()

    torch.testing.assert_allclose(actual, expected)
