# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

from tt_lib import tensor
from models.experimental.bert_large_perf.fused_ops.layernorm import Layernorm


def AddAndNorm(
    gamma: tensor.Tensor, beta: tensor.Tensor, epsilon, var_scaler, H, W, device
):
    """
    Returns a function that performs Eltwise-binary add two
    ``ttlib.tensor.Tensor`` s and then LayerNorm the result.
    """

    layernorm = Layernorm(gamma, beta, epsilon, H, W, device, 1)

    def add_and_norm_(activationa, activationb):
        a_plus_b = tensor.add(activationa, activationb)
        H = activationa.shape()[2]
        lnorm_a_plus_b = layernorm(a_plus_b, var_scaler=var_scaler, overrideH=H)
        return lnorm_a_plus_b

    return add_and_norm_
