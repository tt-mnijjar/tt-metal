# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import math
import ttnn


class cross_attention:
    def __init__(self, device, parameters):
        self.device = device
        self.parameters = parameters

    def prepare_attention_mask(self, attention_mask, target_length, heads=8):
        head_size = heads
        if attention_mask is None:
            return attention_mask

        if attention_mask.shape[-1] != target_length:
            assert False, "Attention Mask has always been None, This is not implemented!"

        return attention_mask

    def concatenate_heads(self, tensor):
        batch_size, heads, seq_len, dim = tensor.shape
        # TILE_LAYOUT is not compatible with tensor shape, hence we used ROW_MAJOR_LAYOUT.
        tensor = ttnn.to_layout(tensor, layout=ttnn.ROW_MAJOR_LAYOUT)
        tensor = ttnn.permute(tensor, (0, 2, 1, 3))
        tensor = ttnn.reshape(tensor, (batch_size, seq_len, dim * heads))
        tensor = ttnn.to_layout(tensor, ttnn.TILE_LAYOUT)
        return tensor

    def split_heads(self, tensor, heads=8):
        tensor = ttnn.unsqueeze_to_4D(
            tensor
        )  # TODO: This is not needed in general but too of SD code needs to be modified
        *batch_sizes, seq_len, dim = tensor.shape  # TODO: this should be 3D with batch_size as first dim
        batch_size = math.prod(batch_sizes)  # TODO:
        # TILE_LAYOUT is not compatible with tensor shape, hence we used ROW_MAJOR_LAYOUT.
        tensor = ttnn.to_layout(tensor, layout=ttnn.ROW_MAJOR_LAYOUT)
        tensor = ttnn.reshape(tensor, (batch_size, seq_len, heads, dim // heads))
        tensor = ttnn.permute(tensor, (0, 2, 1, 3))
        tensor = ttnn.to_layout(tensor, ttnn.TILE_LAYOUT)
        return tensor

    def get_attention_scores(self, query, key, attention_mask=None, scale=None, device=None):
        t_key = ttnn.permute(key, (0, 1, 3, 2))
        temp = ttnn.matmul(query, t_key)

        attention_scores = ttnn.mul(temp, scale)

        if attention_mask is not None:
            attention_scores = ttnn.add(attention_scores, attention_mask)

        attention_probs = ttnn.softmax(attention_scores, dim=-1)

        return attention_probs

    def __call__(
        self,
        hidden_states,
        encoder_hidden_states,
        query_dim: int = None,
        cross_attention_dim=None,
        heads: int = 8,
        dim_head: int = 64,
        attention_mask=None,
        upcast_attention: bool = False,
        upcast_softmax: bool = False,
        cross_attention_kwargs={},
    ):
        _, _, sequence_length, _ = hidden_states.shape
        attention_mask = self.prepare_attention_mask(attention_mask, sequence_length)
        query_weight = self.parameters.to_q.weight

        hidden_states = ttnn.to_layout(hidden_states, ttnn.TILE_LAYOUT)
        query = ttnn.matmul(hidden_states, query_weight)

        query = self.split_heads(query)
        encoder_hidden_states = encoder_hidden_states if encoder_hidden_states is not None else hidden_states

        key_weight = self.parameters.to_k.weight
        encoder_hidden_states = ttnn.to_layout(encoder_hidden_states, ttnn.TILE_LAYOUT)
        key = ttnn.matmul(encoder_hidden_states, key_weight)

        value_weight = self.parameters.to_v.weight
        value = ttnn.matmul(encoder_hidden_states, value_weight)

        key = self.split_heads(key, heads=heads)

        value = self.split_heads(value, heads=heads)

        scale = dim_head**-0.5
        attention_probs = self.get_attention_scores(query, key, attention_mask, scale=scale, device=self.device)

        hidden_states = ttnn.matmul(attention_probs, value)

        hidden_states = self.concatenate_heads(hidden_states)

        out_weight = self.parameters.to_out[0].weight

        hidden_states = ttnn.matmul(hidden_states, out_weight)
        if self.parameters.to_out[0].bias is not None:
            out_bias = self.parameters.to_out[0].bias
            hidden_states = ttnn.add(hidden_states, out_bias)

        return ttnn.unsqueeze_to_4D(hidden_states)  # TODO: This wouldn't be needed if the input was 3D ...
