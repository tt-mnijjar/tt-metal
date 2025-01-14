# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import torch
from torch import nn
import ttnn


class TtFalconMLP(nn.Module):
    def __init__(
        self,
        device,
        model_config,
        parameters,
    ):
        super().__init__()
        self.device = device
        self.model_config = model_config
        self.dense_h_to_4h_weights = parameters.dense_h_to_4h.weight
        self.dense_4h_to_h_weights = parameters.dense_4h_to_h.weight

    def forward(self, x: ttnn.experimental.tensor.Tensor) -> ttnn.experimental.tensor.Tensor:
        # TODO: l1-packing
        ff1_linear = ttnn.linear(
            x,
            self.dense_h_to_4h_weights,
            memory_config=self.model_config["DENSE_H_TO_4H_MM_OUTPUT_MEMCFG"],
            dtype=self.model_config["DENSE_H_TO_4H_MM_OUTPUT_DTYPE"],
            use_1d_systolic_array=True,
            activation="gelu",
        )
        ff2_linear = ttnn.linear(
            ff1_linear,
            self.dense_4h_to_h_weights,
            memory_config=self.model_config["DENSE_4H_TO_H_MM_OUTPUT_MEMCFG"],
            dtype=self.model_config["DENSE_4H_TO_H_MM_OUTPUT_DTYPE"],
            use_1d_systolic_array=True,
        )
        ttnn.deallocate(ff1_linear)

        return ff2_linear
