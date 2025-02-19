# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import ttnn
from tt_lib.fallback_ops import fallback_ops
from models.utility_functions import (
    tt_to_torch_tensor,
    torch_to_tt_tensor_rm,
)
import torch
from typing import Optional, Dict
from models.experimental.functional_stable_diffusion.tt2.ttnn_functional_utility_functions import (
    run_ttnn_conv_with_pre_and_post_tensor_formatting,
    pre_process_input,
    post_process_output,
)


def torch_to_ttnn(input, device, layout=ttnn.TILE_LAYOUT):
    input = ttnn.from_torch(input, ttnn.bfloat16)
    input = ttnn.to_layout(input, layout)
    input = ttnn.to_device(input, device, memory_config=ttnn.DRAM_MEMORY_CONFIG)
    return input


def ttnn_to_torch(input):
    input = ttnn.to_layout(input, ttnn.ROW_MAJOR_LAYOUT)
    input = ttnn.from_device(input)
    input = ttnn.to_torch(input)
    return input


def permute_conv_weights(weight, bias):
    weight = ttnn.to_layout(weight, layout=ttnn.ROW_MAJOR_LAYOUT)
    weight = ttnn.to_torch(weight)
    weight = torch.permute(weight, (2, 3, 0, 1))
    bias = ttnn.to_layout(bias, layout=ttnn.ROW_MAJOR_LAYOUT)
    bias = ttnn.to_torch(bias)
    return weight, bias


config_override = {
    (320, 320, 64, 64): {"act_block_h": 64},
    (640, 640, 32, 32): {"act_block_h": 64},
    (640, 1920, 32, 32): {"act_block_h": 32},
    (640, 1280, 32, 32): {"act_block_h": 32},
    (1280, 1920, 16, 16): {"act_block_h": 32},
    (1280, 1280, 32, 32): {"act_block_h": 32},
    (320, 960, 64, 64): {"act_block_h": 32},
    (640, 960, 32, 32): {"act_block_h": 32},
    (320, 640, 64, 64): {"act_block_h": 32},
    (640, 320, 64, 64): {"act_block_h": 64},
    (640, 640, 64, 64): {"act_block_h": 32},
}

split_chunks = {
    (320, 960, 64, 64): 2,
    (640, 1920, 32, 32): 3,
    (640, 1280, 32, 32): 2,
    (640, 960, 32, 32): 2,
    (1280, 1920, 16, 16): 3,
    (1280, 2560, 8, 8): 2,
    (1280, 2560, 16, 16): 2,
}


class resnetBlock2D:
    def __init__(
        self,
        device,
        parameters,
        reader_patterns_cache,
        batch_size,
        input_height,
        input_width,
        compute_kernel_config=None,
    ):
        self.device = device
        self.parameters = parameters
        self.conv1s = []

        parameters.conv1.weight, parameters.conv1.bias = permute_conv_weights(
            parameters.conv1.weight, parameters.conv1.bias
        )
        out_channels = parameters.conv1.bias.shape[-1]
        in_channels = parameters.conv1.weight.shape[1]

        parameters.conv1.bias = torch.reshape(parameters.conv1.bias, (1, 1, 1, out_channels))
        conv1_split_chunks = 1
        if (out_channels, in_channels, input_height, input_width) in split_chunks:
            conv1_split_chunks = split_chunks[(out_channels, in_channels, input_height, input_width)]
        split_input_channels = in_channels // conv1_split_chunks
        if conv1_split_chunks == 1:
            split_weight_tensors = [parameters.conv1.weight]
        else:
            split_weight_tensors = torch.split(parameters.conv1.weight, split_input_channels, 1)

        for i in range(conv1_split_chunks):
            tt_weight_tensor = ttnn.from_torch(split_weight_tensors[i], ttnn.float32)
            if i == 0:
                tt_bias_tensor = ttnn.from_torch(parameters.conv1.bias, ttnn.float32)
            else:
                # TODO: fix no bias in conv error
                torch_bias_zeros_tensor = torch.zeros(parameters.conv1.bias.shape, dtype=torch.bfloat16).float()
                tt_bias_tensor = ttnn.from_torch(torch_bias_zeros_tensor, ttnn.float32)
            conv1_config_override = {}
            if (out_channels, in_channels, input_height, input_width) in config_override:
                conv1_config_override = config_override[(out_channels, in_channels, input_height, input_width)]
            self.conv1s.append(
                ttnn.Conv2d(
                    split_input_channels,
                    out_channels,
                    kernel_size=(3, 3),
                    stride=(1, 1),
                    padding=(1, 1),
                    dtype=ttnn.bfloat8_b,
                    device=device,
                    use_1d_systolic_array=False,
                    batch_size=batch_size,
                    input_height=input_height,
                    input_width=input_width,
                    reader_patterns_cache=reader_patterns_cache,
                    weight=tt_weight_tensor,
                    bias=tt_bias_tensor,
                    math_fidelity=ttnn.MathFidelity.LoFi,
                    weights_dtype=ttnn.bfloat8_b,
                    conv_blocking_and_parallelization_config_override=conv1_config_override,
                    use_shallow_conv_variant=False,
                    enable_auto_formatting=True,
                    compute_kernel_config=compute_kernel_config,
                )
            )

        conv2_input_height = self.conv1s[0].output_height
        conv2_input_width = self.conv1s[0].output_width
        parameters.conv2.weight, parameters.conv2.bias = permute_conv_weights(
            parameters.conv2.weight, parameters.conv2.bias
        )
        parameters.conv2.bias = torch.reshape(parameters.conv2.bias, (1, 1, 1, out_channels))
        # print("conv2 weight shape=", parameters.conv2.weight.shape)
        # print("conv2 bias shape=", parameters.conv2.bias.shape)
        tt_weight_tensor = ttnn.from_torch(parameters.conv2.weight, ttnn.float32)
        tt_bias_tensor = ttnn.from_torch(parameters.conv2.bias, ttnn.float32)
        conv2_config_override = {}
        if (out_channels, out_channels, input_height, input_width) in config_override:
            conv2_config_override = config_override[(out_channels, out_channels, input_height, input_width)]
        assert out_channels == parameters.conv2.weight.shape[0]
        assert out_channels == parameters.conv2.weight.shape[1]
        self.conv2 = ttnn.Conv2d(
            out_channels,
            out_channels,
            kernel_size=(3, 3),
            stride=(1, 1),
            padding=(1, 1),
            dtype=ttnn.bfloat8_b,
            device=device,
            use_1d_systolic_array=False,
            batch_size=batch_size,
            input_height=conv2_input_height,
            input_width=conv2_input_width,
            reader_patterns_cache=reader_patterns_cache,
            weight=tt_weight_tensor,
            bias=tt_bias_tensor,
            math_fidelity=ttnn.MathFidelity.LoFi,
            weights_dtype=ttnn.bfloat8_b,
            conv_blocking_and_parallelization_config_override=conv2_config_override,
            use_shallow_conv_variant=False,
            enable_auto_formatting=True,
            deallocate_activation=True,
            # reallocate_halo_output=(out_channels, out_channels, input_height, input_width) == (640, 640, 64, 64)
            compute_kernel_config=compute_kernel_config,
        )

        self.output_height = self.conv2.output_height
        self.output_width = self.conv2.output_width
        use_in_shortcut = True if "conv_shortcut" in parameters else False
        if use_in_shortcut:
            parameters.conv_shortcut.weight, parameters.conv_shortcut.bias = permute_conv_weights(
                parameters.conv_shortcut.weight, parameters.conv_shortcut.bias
            )

            convs_input_height = self.conv2.output_height
            convs_input_width = self.conv2.output_width
            parameters.conv_shortcut.bias = torch.reshape(parameters.conv_shortcut.bias, (1, 1, 1, out_channels))
            tt_weight_tensor = ttnn.from_torch(parameters.conv_shortcut.weight, ttnn.float32)
            tt_bias_tensor = ttnn.from_torch(parameters.conv_shortcut.bias, ttnn.float32)
            conv_shortcut_config_override = {}
            # if (out_channels, in_channels, input_height, input_width) in config_override:
            #     conv2_config_override = config_override[(out_channels, in_channels, input_height, input_width)]

            self.conv_shortcut = ttnn.Conv2d(
                parameters.conv_shortcut.weight.shape[1],
                parameters.conv_shortcut.weight.shape[0],
                kernel_size=(1, 1),
                stride=(1, 1),
                padding=(0, 0),
                dtype=ttnn.bfloat8_b,
                device=device,
                use_1d_systolic_array=False,
                batch_size=batch_size,
                input_height=convs_input_height,
                input_width=convs_input_width,
                reader_patterns_cache=reader_patterns_cache,
                weight=tt_weight_tensor,
                bias=tt_bias_tensor,
                math_fidelity=ttnn.MathFidelity.LoFi,
                weights_dtype=ttnn.bfloat8_b,
                conv_blocking_and_parallelization_config_override=conv_shortcut_config_override,
                use_shallow_conv_variant=False,
                enable_auto_formatting=True,
                compute_kernel_config=compute_kernel_config,
            )
            self.output_height = self.conv_shortcut.output_height
            self.output_width = self.conv_shortcut.output_width

    def __call__(
        self,
        input_tensor,
        *,
        temb,
        in_channels,
        temb_channels=1280,
        groups: int = 32,
        time_embedding_norm: str = "default",
        output_scale_factor: float = 1.0,
        out_channels: Optional[int] = None,
        non_linearity="silu",
        pre_norm=True,
        eps=1e-5,
        up=False,
        down=False,
        use_in_shortcut: Optional[bool] = None,
        dtype: Optional[ttnn.DataType] = None,
    ):
        if non_linearity == "mish":
            assert False, "Mish is not implemented!"
        else:
            nonlinearity = ttnn.silu

        out_channels = in_channels if out_channels is None else out_channels
        assert out_channels == self.conv1s[0].out_channels

        hidden_states = input_tensor

        hidden_states = ttnn.group_norm(
            hidden_states,
            num_groups=groups,
            weight=self.parameters.norm1.weight,
            bias=self.parameters.norm1.bias,
            epsilon=eps,
        )
        hidden_states = nonlinearity(hidden_states)

        if up:
            assert False, "Up block within residual block is not implemented!"
        elif down:
            assert False, "Down block within residual block is not implemented"

        # breakpoint()
        hidden_states = pre_process_input(self.device, hidden_states)
        conv1_split_chunks = len(self.conv1s)
        if conv1_split_chunks == 1:
            hidden_states = [hidden_states]
        else:
            split_hidden_states = []
            output_tensor_start_width_dim = 0
            in_channels = self.parameters.conv1.weight.shape[1]
            split_input_channels = in_channels // conv1_split_chunks

            output_tensor_end_width_dim = split_input_channels - 1
            for i in range(conv1_split_chunks):
                split_hidden_states.append(
                    hidden_states[:, :, :, output_tensor_start_width_dim:output_tensor_end_width_dim]
                )
                output_tensor_start_width_dim += split_input_channels
                output_tensor_end_width_dim += split_input_channels
            hidden_states = split_hidden_states
        if conv1_split_chunks == 1:
            hidden_states = self.conv1s[0](hidden_states[0])
        else:
            for i in range(conv1_split_chunks):
                hidden_states[i] = self.conv1s[i](hidden_states[i])
                if i != 0:
                    hidden_states[i] = ttnn.add(hidden_states[i], hidden_states[i - 1])
                    ttnn.deallocate(hidden_states[i - 1])
            hidden_states = hidden_states[-1]

        split_hidden_states = []

        if temb is not None:
            temb = nonlinearity(temb)
            if temb_channels is not None:
                if time_embedding_norm == "default":
                    time_emb_proj_out_channels = out_channels
                elif time_embedding_norm == "scale_shift":
                    time_emb_proj_out_channels = out_channels * 2
                else:
                    raise ValueError(f"unknown time_embedding_norm : {time_embedding_norm} ")
                # temb=ttnn.linear(temb,parameters.time_emb_proj.weight,bias=parameters.time_emb_proj.bias)
                # breakpoint()
                temb = ttnn.matmul(temb, self.parameters.time_emb_proj.weight)
                temb = ttnn.add(temb, self.parameters.time_emb_proj.bias)

            temb = ttnn.permute(temb, (2, 0, 1, 3))

        if temb is not None and time_embedding_norm == "default":
            hidden_states = ttnn.clone(
                hidden_states, memory_config=ttnn.get_memory_config(hidden_states), dtype=ttnn.bfloat16
            )
            hidden_states = ttnn.reshape(
                hidden_states,
                (self.conv2.batch_size, 1, self.conv2.input_height * self.conv2.input_width, out_channels),
            )
            hidden_states = hidden_states + temb

        hidden_states = post_process_output(
            self.device,
            hidden_states,
            self.conv2.batch_size,
            self.conv2.input_height,
            self.conv2.input_width,
            out_channels,
        )
        hidden_states = ttnn.group_norm(
            hidden_states,
            num_groups=groups,
            weight=self.parameters.norm2.weight,
            bias=self.parameters.norm2.bias,
            epsilon=eps,
        )
        hidden_states = nonlinearity(hidden_states)

        hidden_states = run_ttnn_conv_with_pre_and_post_tensor_formatting(
            self.device,
            self.conv2,
            hidden_states,
            self.conv2.batch_size,
            self.conv2.input_height,
            self.conv2.input_width,
            self.conv2.out_channels,
        )

        use_in_shortcut = in_channels != out_channels if use_in_shortcut is None else use_in_shortcut
        if use_in_shortcut:
            input_tensor = run_ttnn_conv_with_pre_and_post_tensor_formatting(
                self.device,
                self.conv_shortcut,
                input_tensor,
                self.conv_shortcut.batch_size,
                self.conv_shortcut.input_height,
                self.conv_shortcut.input_width,
                self.conv_shortcut.out_channels,
            )

        output_sc_recip = 1 / output_scale_factor
        output_tensor = ttnn.add(input_tensor, hidden_states)
        output_tensor = ttnn.mul(output_tensor, output_sc_recip)

        return output_tensor
