# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0


import tt_lib as ttl
from tt_lib.utils import (
    tilize_to_list,
    tilize,
    untilize,
    _nearest_32,
    pad_activation,
)
from models.utility_functions import (
    print_diff_argmax,
    is_close,
    comp_pcc,
)
import torch


def run_tilize_matmul_test(M, K, N, device):
    a_shape = [1, 1, M, K]
    a_shape_padded = [1, 1, _nearest_32(M), K]
    b_shape = [1, 1, K, N]
    output_shape = [1, 1, _nearest_32(M), N]
    A = torch.randn(a_shape)
    A_padded = pad_activation(A)
    B = torch.randn(b_shape) - 0.95

    a = ttl.tensor.Tensor(
        A.flatten().tolist(),
        a_shape,
        ttl.tensor.DataType.BFLOAT16,
        ttl.tensor.Layout.ROW_MAJOR,
        device,
    )
    a_t = ttl.tensor.tilize_with_zero_padding(a)
    print("Shape of A_t - " + str(a_t.shape()))
    b_t = ttl.tensor.Tensor(
        tilize_to_list(B),
        b_shape,
        ttl.tensor.DataType.BFLOAT16,
        ttl.tensor.Layout.TILE,
        device,
    )
    print("Shape of B_t - " + str(b_t.shape()))
    t2 = ttl.tensor.bmm(a_t, b_t)
    assert list(t2.shape()) == output_shape
    tt_host_rm = t2.cpu().to_torch()
    pyt_got_back = tt_host_rm.reshape(output_shape)
    # TODO: add support to remove padding in untilize
    pyt_got_back_rm = untilize(pyt_got_back)

    ref_bmm = torch.matmul(A_padded.reshape(a_shape_padded[1:]), B.reshape(b_shape[1:]))
    ref_bmm = ref_bmm.reshape(output_shape)
    passing_pcc, output_pcc = comp_pcc(ref_bmm, pyt_got_back_rm, 0.99)
    print("Passing=", passing_pcc)
    print("Output pcc=", output_pcc)

    assert passing_pcc


def test_tilize_hpadding_matmul(device):
    run_tilize_matmul_test(4, 32 * 9, 32, device)
