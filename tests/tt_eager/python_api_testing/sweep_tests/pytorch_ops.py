import torch
from tt_lib.utils import (
    _nearest_32 as nearest_32,
    tilize as tilize_util,
    untilize as untilize_util,
)

################################################
################# Helper-Funcs #################
################################################


def linear(x, weight, bias=None, *args, **kwargs):
    while len(weight.shape) > 2:
        weight = weight.squeeze(0)
    while bias is not None and len(bias.shape) > 1:
        bias = bias.squeeze(0)

    return torch.nn.functional.linear(x, weight, bias)


################################################
#################### TT-DNN ####################
################################################
def move(x, *args, **kwargs):
    return x


# Stats Ops
def var_hw(x, *args, **kwargs):
    return torch.var(x, [2, 3], keepdim=True)


def std_hw(x, *args, **kwargs):
    return torch.std(x, [2, 3], keepdim=True)


def mean_hw(x, *args, **kwargs):
    return torch.mean(x, [2, 3], keepdim=True)


def normalize_hw(x, *args, **kwargs):
    mx = mean_hw(x)
    sx = std_hw(x)
    for i in range(x.shape[0]):
        for j in range(x.shape[1]):
            x[i, j, :, :] = (x[i, j, :, :] - mx[i, j, :, :]) / sx[i, j, :, :]
    return x


# Ternary Ops
def sum(x, *args, dim, **kwargs):
    return torch.sum(x, dim=dim, keepdim=True)


def where(x, y, z, *args, **kwargs):
    return torch.where(x > 0, y, z)


def arange(x, *args, start, end, step=1, **kwargs):
    return torch.arange(start, end, step)


# Unary Ops
def hypot(x, y, *args, **kwargs):
    return torch.hypot(x, y)


def cbrt(x, *args, **kwargs):
    result = x.sign() * x.abs().pow(1.0 / 3.0)
    return result


def rad2deg(x, *args, **kwargs):
    result = torch.rad2deg(x)
    return result


def deg2rad(x, *args, **kwargs):
    result = torch.deg2rad(x)
    return result


def threshold(x, *args, threshold, value, **kwargs):
    result = torch.threshold(x, threshold, value)
    return result


def relu6(x, *args, **kwargs):
    result = torch.nn.functional.relu6(x)
    return result


def softsign(x, *args, **kwargs):
    result = torch.nn.functional.softsign(x)
    return result


def leaky_relu(x, *args, negative_slope, **kwargs):
    result = torch.nn.functional.leaky_relu(x, negative_slope)
    return result


def softshrink(x, *args, _lambda, **kwargs):
    result = torch.nn.functional.softshrink(x, _lambda)
    return result


def hardshrink(x, *args, _lambda, **kwargs):
    result = torch.nn.functional.hardshrink(x, _lambda)
    return result


def bias_gelu(x, *args, bias, **kwargs):
    result = torch.nn.functional.gelu(x + bias)
    return result


def hardtanh(x, *args, **kwargs):
    result = torch.nn.functional.hardtanh(x)
    return result


def clip(x, *args, low, high, **kwargs):
    result = torch.clip(x, low, high)
    return result


# Unary Ops and Composite Unary
def bitwise_complement(x, *args, **kwargs):
    result = torch.bitwise_not(x)
    return result


def logical_not(x, *args, **kwargs):
    result = torch.logical_not(x).to(torch.int32)
    return result


def cosh(x, *args, **kwargs):
    result = torch.cosh(x)
    return result


def sinh(x, *args, **kwargs):
    result = torch.sinh(x)
    return result


def power(x, *args, exponent, **kwargs):
    result = x**exponent
    return result


def polyval(x, *args, coeffs, **kwargs):
    result = 0.0
    for coeff in coeffs:
        result = result * x + coeff
    return result


def relu_max(x, *args, upper_limit, **kwargs):
    return torch.relu(torch.min(x, torch.tensor(upper_limit)))


def relu_min(x, *args, lower_limit, **kwargs):
    return torch.max(x, torch.tensor(lower_limit))


def abs(x, *args, **kwargs):
    return x.abs()


def ltz(x, *args, **kwargs):
    return (x < 0.0).to(x.dtype)


def gtz(x, *args, **kwargs):
    return (x > 0.0).to(x.dtype)


def lez(x, *args, **kwargs):
    return (x <= 0.0).to(x.dtype)


def gez(x, *args, **kwargs):
    return (x >= 0.0).to(x.dtype)


def eqz(x, *args, **kwargs):
    return (x == 0.0).to(x.dtype)


def nez(x, *args, **kwargs):
    return (x != 0.0).to(x.dtype)


def sign(x, *args, **kwargs):
    return torch.sign(x)


def datacopy(x, *args, **kwargs):
    return x


def neg(x, *args, **kwargs):
    return -x


def square(x, *args, **kwargs):
    return torch.square(x)


def log1p(x, *args, **kwargs):
    return torch.log1p(x)


def softplus(x, *args, **kwargs):
    return torch.nn.functional.softplus(x)


def add1(x, *args, **kwargs):
    return 1 + x


def mish(x, *args, **kwargs):
    return x * torch.tanh(softplus(x))


def recip(x, *args, **kwargs):
    return torch.reciprocal(x)


def exp(x, *args, **kwargs):
    return torch.exp(x)


def exp2(x, *args, **kwargs):
    return torch.exp2(x)


def expm1(x, *args, **kwargs):
    return torch.expm1(x)


def sqrt(x, *args, **kwargs):
    return torch.sqrt(x)


def gelu(x, *args, **kwargs):
    return torch.nn.functional.gelu(x)


def rsqrt(x, *args, **kwargs):
    return torch.rsqrt(x)


def relu(x, *args, **kwargs):
    return torch.nn.functional.relu(x)


def sigmoid(x, *args, **kwargs):
    return torch.sigmoid(x)


def log_sigmoid(x, *args, **kwargs):
    result = torch.nn.functional.logsigmoid(x)
    return result


def heaviside(x, *args, **kwargs):
    value = kwargs.pop("value")
    result = torch.heaviside(x, torch.tensor(value, dtype=torch.bfloat16))
    return result


def erf(x, *args, **kwargs):
    return torch.erf(x)


def erfc(x, *args, **kwargs):
    return torch.erfc(x)


def hardsigmoid(x, *args, **kwargs):
    return torch.nn.functional.hardsigmoid(x)


def hardswish(x, *args, **kwargs):
    return torch.nn.functional.hardswish(x)


def log(x, *args, **kwargs):
    return torch.log(x)


def log2(x, *args, **kwargs):
    return torch.log2(x)


def log10(x, *args, **kwargs):
    return torch.log10(x)


def tanh(x, *args, **kwargs):
    return torch.tanh(x)


def tanhshrink(x, *args, **kwargs):
    return torch.nn.functional.tanhshrink(x)


def signbit(x, *args, **kwargs):
    return torch.signbit(x)


def sin(x, *args, **kwargs):
    return torch.sin(x)


def asin(x, *args, **kwargs):
    return torch.asin(x)


def atan(x, *args, **kwargs):
    return torch.atan(x)


def atanh(x, *args, **kwargs):
    return torch.atanh(x)


def acos(x, *args, **kwargs):
    return torch.acos(x)


def cos(x, *args, **kwargs):
    return torch.cos(x)


def asinh(x, *args, **kwargs):
    return torch.asinh(x)


def acosh(x, *args, **kwargs):
    return torch.acosh(x)


def elu(x, *args, alpha, **kwargs):
    return torch.nn.functional.elu(x, alpha)


def swish(x, *args, **kwargs):
    return torch.nn.functional.silu(x)


def silu(x, *args, **kwargs):
    return torch.nn.functional.silu(x)


def div_unary(x, *args, scalar, **kwargs):
    result = torch.div(x, scalar)
    return result


def mul_unary(x, *args, scalar, **kwargs):
    result = torch.mul(x, scalar)
    return result


def sub_unary(x, *args, scalar, **kwargs):
    result = torch.sub(x, scalar)
    return result


def add_unary(x, *args, scalar, **kwargs):
    result = torch.add(x, scalar)
    return result


def zeros_like(x, *args, **kwargs):
    result = torch.zeros_like(x)
    return result


def ones_like(x, *args, **kwargs):
    result = torch.ones_like(x)
    return result


def full_like(x, *args, scalar, **kwargs):
    result = torch.full_like(x, scalar)
    return result


def zeros(x, *args, **kwargs):
    result = torch.zeros(x.shape)
    return result


def ones(x, *args, **kwargs):
    result = torch.ones(x.shape)
    return result


def full(x, *args, scalar, **kwargs):
    result = torch.full(x.shape, scalar)
    return result


## Trinary op
def mac(x, y, z, *args, **kwargs):
    return x * y + z


def addcmul(x, y, z, *args, value, **kwargs):
    result = torch.addcmul(x, y, z, value=value)
    return result


def addcdiv(x, y, z, *args, value, **kwargs):
    result = torch.addcdiv(x, y, z, value=value)
    return result


def lerp_ternary(x, y, z, *args, **kwargs):
    return torch.lerp(x, y, z)


## Binary Ops
def atan2(x, y, *args, **kwargs):
    return torch.atan2(y, x)


def lerp_binary(x, y, *args, weight, **kwargs):
    return torch.lerp(x, y, weight)



def xlogy(x, y, *args, **kwargs):
    return torch.xlogy(x, y)

def ldexp(x, y, *args, **kwargs):
    return torch.ldexp(x, y)


def subalpha(x, y, *args, alpha, **kwargs):
    return torch.sub(x, y, alpha=alpha)


def lte(x, y, *args, **kwargs):
    return x <= y


def lt(x, y, *args, **kwargs):
    return x < y


def gte(x, y, *args, **kwargs):
    return x >= y


def gt(x, y, *args, **kwargs):
    return x > y


def eq(x, y, *args, **kwargs):
    return x == y


def ne(x, y, *args, **kwargs):
    return x != y


def max(x, y, *args, **kwargs):
    return torch.max(x, y)


def min(x, y, *args, **kwargs):
    return torch.min(x, y)


def squared_difference(x, y, *args, **kwargs):
    t_diff = torch.sub(x, y)
    return torch.square(t_diff)


def logaddexp(x, y, *args, **kwargs):
    return torch.logaddexp(x, y)


def logaddexp2(x, y, *args, **kwargs):
    return torch.logaddexp2(x, y)


def add(x, y, *args, **kwargs):
    return torch.add(x, y)


def sub(x, y, *args, **kwargs):
    return torch.sub(x, y)


def mul(x, y, *args, **kwargs):
    return torch.mul(x, y)


def matmul(x, y, *args, **kwargs):
    return torch.matmul(x, y)


def outer(x, y, *args, **kwargs):
    return torch.outer(x.squeeze(), y.squeeze())


def reduce_sum(x, dims=None, keepdim=True, *args, **kwargs):
    return torch.sum(x, dims, keepdim)


def reduce_max(x, dims=None, keepdim=True, *args, **kwargs):
    return torch.amax(x, dims, keepdim)


def flatten(x, *args, **kwargs):
    return torch.flatten(x)


def transpose(x, dim0=-2, dim1=-1, *args, **kwargs):
    return torch.transpose(x, dim0, dim1)


def permute(x, *args, permute_dims, **kwargs):
    return torch.permute(x, permute_dims)


def reshape(x, *args, reshape_dims, **kwargs):
    return torch.reshape(x, reshape_dims)


def tilize(x, *args, **kwargs):
    return tilize_util(x)


def untilize(x, *args, **kwargs):
    return untilize_util(x)


def tilize_with_zero_padding(x, *args, **kwargs):
    return tilize_util(
        torch.nn.functional.pad(
            x,
            (
                0,
                nearest_32(x.shape[-1]) - x.shape[-1],
                0,
                nearest_32(x.shape[-2]) - x.shape[-2],
            ),
        )
    )


def tilize_with_val_padding(
    x, output_tensor_shape, input_tensor_start, pad_value, *args, **kwargs
):
    pad = torch.nn.functional.pad(
        x,
        tuple(
            j
            for i in reversed(range(len(x.shape)))
            for j in (input_tensor_start[i], output_tensor_shape[i] - x.shape[i])
        ),
        value=pad_value,
    )
    tilized = tilize_util(pad)
    return tilized


def untilize_with_unpadding(x, output_tensor_start, output_tensor_end, *args, **kwargs):
    untilized = untilize_util(x)
    unpad = untilized[
        output_tensor_start[0] : output_tensor_end[0] + 1,
        output_tensor_start[1] : output_tensor_end[1] + 1,
        output_tensor_start[2] : output_tensor_end[2] + 1,
        output_tensor_start[3] : output_tensor_end[3] + 1,
    ]
    return unpad


################################################
#################### Tensor ####################
################################################
def pad(x, *args, output_tensor_shape, input_tensor_start, pad_value, **kwargs):
    input_tensor_shape = x.shape
    input_tensor_end = tuple(
        input_tensor_start[i] + input_tensor_shape[i]
        for i in range(len(input_tensor_shape))
    )
    out = torch.full(output_tensor_shape, pad_value, dtype=torch.bfloat16)
    out[
        input_tensor_start[0] : input_tensor_end[0],
        input_tensor_start[1] : input_tensor_end[1],
        input_tensor_start[2] : input_tensor_end[2],
        input_tensor_start[3] : input_tensor_end[3],
    ] = x

    return out


def unpad(x, *args, output_tensor_start, output_tensor_end, **kwargs):
    out = x[
        output_tensor_start[0] : output_tensor_end[0] + 1,
        output_tensor_start[1] : output_tensor_end[1] + 1,
        output_tensor_start[2] : output_tensor_end[2] + 1,
        output_tensor_start[3] : output_tensor_end[3] + 1,
    ]

    return out


def pad_to_tile(x, pad_value, *args, **kwargs):
    input_tensor_shape = x.shape
    output_tensor_shape = [
        *input_tensor_shape[:-2],
        nearest_32(input_tensor_shape[-2]),
        nearest_32(input_tensor_shape[-1]),
    ]
    out = torch.full(output_tensor_shape, pad_value, dtype=torch.bfloat16)
    out[
        0 : input_tensor_shape[0],
        0 : input_tensor_shape[1],
        0 : input_tensor_shape[2],
        0 : input_tensor_shape[3],
    ] = x

    return out


def unpad_from_tile(x, output_tensor_shape, *args, **kwargs):
    out = x[
        0 : output_tensor_shape[0],
        0 : output_tensor_shape[1],
        0 : output_tensor_shape[2],
        0 : output_tensor_shape[3],
    ]

    return out