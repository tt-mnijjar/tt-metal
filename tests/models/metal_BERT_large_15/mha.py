import pytest
from loguru import logger
import math
from pathlib import Path
import sys

f = f"{Path(__file__).parent}"
sys.path.append(f"{f}/..")
sys.path.append(f"{f}/../..")
sys.path.append(f"{f}/../../..")
sys.path.append(f"{f}/../../../..")

import torch
from transformers import BertForQuestionAnswering
import numpy as np

import tt_lib as ttl
from tt_lib.utils import pad_activation, pad_weight, print_diff_argmax
from models.utility_functions import enable_persistent_kernel_cache, comp_pcc, comp_allclose, profiler

from tests.models.metal_BERT_large_15.model_config import get_model_config


def torch2tt_tensor(py_tensor: torch.Tensor, tt_device, dtype, mem_config):
    size = list(py_tensor.size())

    while len(size) < 4:
        size.insert(0, 1)

    tt_tensor = (
        ttl.tensor.Tensor(
            py_tensor.reshape(-1).tolist(),
            size,
            dtype,
            ttl.tensor.Layout.ROW_MAJOR,
        )
        .to(ttl.tensor.Layout.TILE)
        .to(tt_device, mem_config)
    )

    return tt_tensor


def tt2torch_tensor(tt_tensor):

    tt_output = tt_tensor.cpu()
    if tt_output.layout() != ttl.tensor.Layout.ROW_MAJOR:
        tt_output = tt_output.to(ttl.tensor.Layout.ROW_MAJOR)
    dtype = {
        ttl.tensor.DataType.FLOAT32:   torch.float,
        ttl.tensor.DataType.BFLOAT16:  torch.bfloat16,
        ttl.tensor.DataType.BFLOAT8_B: torch.float,
    }[tt_tensor.dtype()]

    py_output = tt_output.to_torch()
    return py_output


def mha(qw, qb, kw, kb, vw, vb, hidden_dim, num_heads, device, model_config):
    assert isinstance(num_heads, int) and num_heads > 0

    # Weights pre-transposed on host​. No on-the fly transpose of W​
    qw = torch.transpose(qw, -1, -2)
    kw = torch.transpose(kw, -1, -2)
    vw = torch.transpose(vw, -1, -2)

    qkv_weight = torch.cat((qw, kw, vw), -1)
    qkv_bias = torch.cat((qb, kb, vb), -1)

    qkv_weight = torch2tt_tensor(
        qkv_weight,
        device,
        model_config["OP1_FUSED_QKV_MM_WEIGHTS_DTYPE"],
        model_config["OP1_FUSED_QKV_MM_WEIGHTS_MEMCFG"],
    )
    qkv_bias = torch2tt_tensor(
        qkv_bias,
        device,
        model_config["OP1_FUSED_QKV_MM_BIAS_DTYPE"],
        model_config["OP1_FUSED_QKV_MM_BIAS_MEMCFG"],
    )

    # Used to scale down the input to the softmax
    freciprocal_of_sqrt_hidden_dim = 1 / math.sqrt(hidden_dim // num_heads)

    def op1_qkv_fused(activation, qkv_weight, qkv_bias):
        # profiler.start("___op1_qkv_fused")
        qkv = ttl.tensor.bert_large_fused_qkv_matmul(
            activation,
            qkv_weight,
            qkv_bias,
            output_mem_config=model_config["OP1_FUSED_QKV_MM_OUTPUT_MEMCFG"],
            output_dtype=model_config["OP1_FUSED_QKV_MM_OUTPUT_DTYPE"],
        )
        # profiler.end("___op1_qkv_fused")

        return qkv

    def op2to6_create_qkv_heads(qkv):
        # profiler.start("___op2to6_create_qkv_heads")
        q_heads, kt_heads, v_heads = ttl.operations.primary.transformers.split_fused_qkv_and_split_heads(
            qkv, ttl.tensor.CoreCoord(12, 9), output_mem_config=model_config["OP2TO6_SPLIT_QKV_HEADS_OUTPUT_MEMCFG"]
        )
        # profiler.end("___op2to6_create_qkv_heads")

        return q_heads, kt_heads, v_heads

    def op7_bmm(Q_heads, K_T_heads):
        # profiler.start("___op7_bmm")
        qkt = ttl.tensor.bert_large_pre_softmax_bmm(
            Q_heads,
            K_T_heads,
            output_mem_config=model_config["OP7_PRE_SOFTMAX_BMM_OUTPUT_MEMCFG"],
            output_dtype=model_config["OP7_PRE_SOFTMAX_BMM_OUTPUT_DTYPE"],
        )
        # profiler.end("___op7_bmm")

        return qkt

    def op8_scale_mask_softmax(qkt, attention_mask):
        # Attention scores computation
        # profiler.start("___op8_scale_mask_softmax")

        # Input and output tensors of this fused op is: [9, 1, 6144, 384] instead of [9, 16, 384, 384]
        # No-op reshapes are handled within pre-softmax (op 7) and post-softmax bmms (op 9)
        if attention_mask is not None:
            attention_scores = ttl.operations.primary.transformers.scale_mask_softmax_in_place(
                qkt, freciprocal_of_sqrt_hidden_dim, attention_mask
            )
        else:
            # No pass in mha sub-graph or full bert encoder uses this anymore
            assert (
                False
            ), "Must provide attention_mask to scale_mask_softmax in mha sub-graph!"
        # profiler.end("___op8_scale_mask_softmax")

        return attention_scores

    def op9_bmm(attention_scores, V_heads):
        # profiler.start("___op9_bmm")
        weighted_activation = ttl.tensor.bert_large_post_softmax_bmm(
            attention_scores,
            V_heads,
            output_mem_config=model_config["OP9_POST_SOFTMAX_BMM_OUTPUT_MEMCFG"],
            output_dtype=model_config["OP9_POST_SOFTMAX_BMM_OUTPUT_DTYPE"],
        )
        # profiler.end("___op9_bmm")

        return weighted_activation

    def op10_unmake_attention_heads(x):
        if num_heads == 1:
            # profiler.start("___op10_unmake_attention_heads")
            # profiler.end("___op10_unmake_attention_heads")
            return x
        else:
            # profiler.start("___op10_unmake_attention_heads")
            retval = ttl.operations.primary.transformers.concatenate_heads(
                x, ttl.tensor.CoreCoord(12, 9), output_mem_config=model_config["OP10_CONCATENATE_ATTENTION_HEADS_OUTPUT_MEMCFG"]
            )
            # profiler.end("___op10_unmake_attention_heads")

            return retval

    def mha_(activation, attention_mask):
        # profiler.start("__mha")
        qkv = op1_qkv_fused(activation, qkv_weight, qkv_bias)
        # activation.deallocate()

        Q_heads, K_T_heads, V_heads = op2to6_create_qkv_heads(qkv)
        qkv.deallocate()

        qkt = op7_bmm(Q_heads, K_T_heads)
        Q_heads.deallocate()
        K_T_heads.deallocate()

        attention_scores = op8_scale_mask_softmax(qkt, attention_mask)
        # Should be a no-op deallocate since it was moved?
        # qkt.deallocate()
        weighted_activation = op9_bmm(attention_scores, V_heads)
        attention_scores.deallocate()
        V_heads.deallocate()

        res = op10_unmake_attention_heads(
            weighted_activation
        )  # [N, num heads, seq len, hid size / num heads] -> [N, seq len, hid size]
        # profiler.end("__mha")
        weighted_activation.deallocate()

        return res

    return mha_


class TtMultiHeadAttentionModel(torch.nn.Module):
    def __init__(self, config, encoder_idx, state_dict, device, model_config):
        super().__init__()
        qw = pad_weight(
            state_dict[f"bert.encoder.layer.{encoder_idx}.attention.self.query.weight"]
        )
        qb = pad_weight(
            state_dict[f"bert.encoder.layer.{encoder_idx}.attention.self.query.bias"]
        )
        kw = pad_weight(
            state_dict[f"bert.encoder.layer.{encoder_idx}.attention.self.key.weight"]
        )
        kb = pad_weight(
            state_dict[f"bert.encoder.layer.{encoder_idx}.attention.self.key.bias"]
        )
        vw = pad_weight(
            state_dict[f"bert.encoder.layer.{encoder_idx}.attention.self.value.weight"]
        )
        vb = pad_weight(
            state_dict[f"bert.encoder.layer.{encoder_idx}.attention.self.value.bias"]
        )

        # Hidden dim
        hidden_dim = qw.shape[-1]

        self.mha = mha(
            qw,
            qb,
            kw,
            kb,
            vw,
            vb,
            hidden_dim,
            config.num_attention_heads,
            device,
            model_config,
        )

    def forward(self, activation, attention_mask=None):
        result = self.mha(activation, attention_mask)
        return result


class PytorchMultiHeadAttentionModel(torch.nn.Module):
    def __init__(self, hugging_face_reference_model):
        super().__init__()
        self.mha = hugging_face_reference_model.bert.encoder.layer[0].attention.self

        # Disable dropout
        self.mha.eval()

    def forward(self, x, attention_mask):
        result = self.mha(x, attention_mask)[0]
        return result


def run_mha_inference(
    model_version, batch, seq_len, pcc, model_config, model_location_generator
):
    device = ttl.device.CreateDevice(ttl.device.Arch.GRAYSKULL, 0)
    # Initialize the device
    ttl.device.InitializeDevice(device)

    model_name = str(model_location_generator(model_version, model_subdir = "Bert"))

    hugging_face_reference_model = BertForQuestionAnswering.from_pretrained(
        model_name, torchscript=False
    )
    tt_mha_model = TtMultiHeadAttentionModel(
        hugging_face_reference_model.config,
        0,
        hugging_face_reference_model.state_dict(),
        device,
        model_config,
    )
    pytorch_mha_model = PytorchMultiHeadAttentionModel(hugging_face_reference_model)

    # Prepare input
    torch.manual_seed(0)
    mha_input = (
        torch.rand(batch, 1, seq_len, hugging_face_reference_model.config.hidden_size)
        * 2
    ) - 1
    bert_attention_mask = torch.zeros(batch, 1, 1, seq_len)
    extended_bert_attention_mask = torch.zeros(batch, 1, 32, seq_len)
    pytorch_out = pytorch_mha_model(
        mha_input.squeeze(1), bert_attention_mask
    ).unsqueeze(1)

    pad_mha_input = pad_activation(mha_input)
    tt_mha_input = ttl.tensor.Tensor(
        pad_mha_input.reshape(-1).tolist(),
        pad_mha_input.shape,
        model_config["OP1_FUSED_QKV_MM_INPUT_DTYPE"],
        ttl.tensor.Layout.ROW_MAJOR,
    ).to(ttl.tensor.Layout.TILE)
    tt_mha_input = tt_mha_input.to(
        device, model_config["OP1_FUSED_QKV_MM_INPUT_MEMCFG"]
    )

    tt_bert_attention_mask = (
        ttl.tensor.Tensor(
            extended_bert_attention_mask.reshape(-1).tolist(),
            extended_bert_attention_mask.shape,
            model_config["OP8_SOFTMAX_ATTENTION_MASK_DTYPE"],
            ttl.tensor.Layout.ROW_MAJOR,
        )
        .to(ttl.tensor.Layout.TILE)
        .to(device, model_config["OP8_SOFTMAX_ATTENTION_MASK_MEMCFG"])
    )

    tt_out = tt_mha_model(tt_mha_input, tt_bert_attention_mask).cpu()
    tt_out1 = tt_out.to(ttl.tensor.Layout.ROW_MAJOR).to_torch().reshape(
        tt_out.shape()
    )

    ttl.device.CloseDevice(device)

    passing, output = comp_pcc(pytorch_out, tt_out1, pcc)
    logger.info(f"Output {output}")
    _, output = comp_allclose(
        pytorch_out, tt_out1, 0.5, 0.5
    )  # Only interested in reporting atol/rtol, using PCC for pass/fail
    logger.info(f"Output {output}")
    if not passing:
        logger.error(f"Output PCC < {pcc}")

    if model_config["DEFAULT_DTYPE"] == ttl.tensor.DataType.BFLOAT8_B and not passing:
        pytest.xfail("PCC is garbage for BFLOAT8_B. Numbers are for perf only!")

    assert passing
    # print_diff_argmax(pytorch_out, tt_out1)
    # assert np.allclose(pytorch_out.detach().numpy(), tt_out1, 1e-5, 0.17)


@pytest.mark.parametrize(
    "batch, model_config_str",
    (
        (9, "BFLOAT8_B-DRAM"),
        (9, "BFLOAT16-DRAM"),
        (9, "BFLOAT8_B-L1"),
        (9, "BFLOAT16-L1"),
        (9, "MIXED_PRECISION_BATCH9"),
        (8, "MIXED_PRECISION_BATCH8"),
    ),
    ids=[
        "batch_9-BFLOAT8_B-DRAM",
        "batch_9-BFLOAT16-DRAM",
        "batch_9-BFLOAT8_B-L1",
        "batch_9-BFLOAT16-L1",
        "batch_9-MIXED_PRECISION_BATCH9",
        "batch_8-MIXED_PRECISION_BATCH8",
    ],
)
@pytest.mark.parametrize(
    "model_version, seq_len, pcc",
    (("phiyodr/bert-large-finetuned-squad2", 384, 0.99),),
    ids=["BERT_LARGE"],
)
def test_mha_inference(
    model_version,
    batch,
    seq_len,
    pcc,
    model_config_str,
    model_location_generator,
    request,
):
    model_config = get_model_config(model_config_str)

    ttl.profiler.set_profiler_location(
        f"tt_metal/tools/profiler/logs/BERT_large_mha_{request.node.callspec.id}"
    )

    run_mha_inference(
        model_version,
        batch,
        seq_len,
        pcc,
        model_config,
        model_location_generator,
    )