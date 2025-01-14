# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import torch
import tt_lib

from models.experimental.convnet_mnist.reference.convnet import ConvNet
from tt_lib.fallback_ops import fallback_ops
from models.utility_functions import torch2tt_tensor


class TtConvNet(torch.nn.Module):
    def __init__(self, device=None, state_dict=None):
        super().__init__()
        self.device = device

        self.tt_conv1_weight = torch2tt_tensor(
            state_dict[f"conv1.weight"], None, tt_layout=tt_lib.tensor.Layout.ROW_MAJOR
        )
        self.tt_conv1_bias = torch2tt_tensor(state_dict[f"conv1.bias"], None, tt_layout=tt_lib.tensor.Layout.ROW_MAJOR)

        self.tt_conv2_weight = torch2tt_tensor(
            state_dict[f"conv2.weight"], None, tt_layout=tt_lib.tensor.Layout.ROW_MAJOR
        )
        self.tt_conv2_bias = torch2tt_tensor(state_dict[f"conv2.bias"], None, tt_layout=tt_lib.tensor.Layout.ROW_MAJOR)

        self.linear1_weights = torch2tt_tensor(
            state_dict[f"fc1.weight"], device, tt_layout=tt_lib.tensor.Layout.ROW_MAJOR
        )
        self.linear1_bias = torch2tt_tensor(state_dict[f"fc1.bias"], device, tt_layout=tt_lib.tensor.Layout.ROW_MAJOR)

        self.linear2_weights = torch2tt_tensor(
            state_dict[f"fc2.weight"], device, tt_layout=tt_lib.tensor.Layout.ROW_MAJOR
        )
        self.linear2_bias = torch2tt_tensor(state_dict[f"fc2.bias"], device, tt_layout=tt_lib.tensor.Layout.ROW_MAJOR)

        self.linear1_weights = tt_lib.tensor.transpose(self.linear1_weights, -2, -1)
        self.linear2_weights = tt_lib.tensor.transpose(self.linear2_weights, -2, -1)

        self.max_pool2d = fallback_ops.MaxPool2d(2)

    def forward(self, tt_x: tt_lib.tensor.Tensor) -> tt_lib.tensor.Tensor:
        out = fallback_ops.conv2d(tt_x, self.tt_conv1_weight, self.tt_conv1_bias)
        out = tt_lib.tensor.relu(out)
        out = self.max_pool2d(out)

        out = fallback_ops.conv2d(out, self.tt_conv2_weight, self.tt_conv2_bias)
        out = tt_lib.tensor.relu(out)
        out = self.max_pool2d(out)

        last_dim_size = out.shape()[-1] * out.shape()[-2] * out.shape()[-3]
        out = fallback_ops.reshape(out, out.shape()[0], 1, 1, last_dim_size)

        out = tt_lib.tensor.matmul(out, self.linear1_weights)
        out = tt_lib.tensor.bcast(
            out,
            self.linear1_bias,
            tt_lib.tensor.BcastOpMath.ADD,
            tt_lib.tensor.BcastOpDim.H,
        )
        out = tt_lib.tensor.relu(out)
        out = tt_lib.tensor.matmul(out, self.linear2_weights)
        out = tt_lib.tensor.bcast(
            out,
            self.linear2_bias,
            tt_lib.tensor.BcastOpMath.ADD,
            tt_lib.tensor.BcastOpDim.H,
        )

        return fallback_ops.softmax(out, -1)


def _convnet_mnist(device, state_dict) -> TtConvNet:
    return TtConvNet(device, state_dict)


def convnet_mnist(device) -> TtConvNet:
    checkpoint = torch.load("/mnt/MLPerf/tt_dnn-models/ConvNetMNIST/convnet_mnist.pt")
    pt_model = ConvNet()
    pt_model.load_state_dict(checkpoint)
    pt_model.eval()

    tt_model = _convnet_mnist(device=device, state_dict=pt_model.state_dict())

    tt_model.eval()
    return tt_model, pt_model
