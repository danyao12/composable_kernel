#ifndef CK_ELEMENT_WISE_OPERATION_HPP
#define CK_ELEMENT_WISE_OPERATION_HPP

namespace ck {
namespace tensor_operation {
namespace element_wise {

struct PassThrough
{
    template <typename T>
    __host__ __device__ void operator()(T& y, const T& x) const
    {
        y = x;
    }

    // TODO remove this
    template <typename T>
    __host__ __device__ constexpr T operator()(T v) const
    {
        return v;
    }
};

struct AddRelu
{
    template <typename T>
    __host__ __device__ constexpr void operator()(T& y, const T& x0, const T& x1) const
    {
        T a = x0 + x1;
        y   = a > 0 ? a : 0;
    }

    // TODO remove this
    template <typename T1>
    __host__ constexpr float operator()(float v0, T1 v1) const
    {
        float b = v0 + v1;
        float c = b > 0 ? b : 0;

        return c;
    }

    // TODO remove this
    template <typename T1>
    __device__ constexpr float operator()(float v0, T1 v1) const
    {
#if 0
        float a = v1 + v0;
        float b = max(a, float(0));

        return b;
#else
        float b = v1 + v0;
        float c = b > 0 ? b : 0;

        return c;
#endif
    }
};

struct AddReluAdd
{
    template <typename T>
    __host__ __device__ constexpr void operator()(T& y, const T& x0, const T& x1, const T& x2) const
    {
        T a = x0 + x1;
        T b = a > 0 ? a : 0;
        y   = b + x2;
    }

    // TODO remove this
    template <typename T1, typename T2>
    __host__ constexpr float operator()(float v0, T1 v1, T2 v2) const
    {
        float b = v0 + v1;
        float c = b > 0 ? b : 0;
        float d = c + v2;

        return d;
    }

    // TODO remove this
    template <typename T1, typename T2>
    __device__ constexpr float operator()(float v0, T1 v1, T2 v2) const
    {
#if 0
        float a = v1 + v0;
        float b = max(a, float(0));
        float c = b + v2;

        return c;
#else
        float b = v1 + v2;
        float c = (v0 > -v1) ? b + v0 : v2;

        return c;
#endif
    }
};

struct Relu
{
    //__host__ __device__ constexpr float operator()(const float& x) const { return x > 0 ? x : 0; }
    template <typename X>
    __host__ __device__ constexpr half_t operator()(const X& x) const
    {
        return x > 0 ? x : 0;
    }
};

struct Int4Quant
{
    // FIXME: We just need one scale for Relu / Leaky Relu / PRelu
    __host__ __device__ Int4Quant(float scaleRelu) : scale_(scaleRelu) {}

    inline __host__ __device__ constexpr int8_t quant(int32_t x) const
    {
        float gemm_c_float = static_cast<float>(x);
        float requant      = scale_ * gemm_c_float;

        int8_t y = static_cast<int8_t>(requant > 63 ? 63 : requant < -64 ? -64 : requant);

        return y;
    }

    __host__ __device__ constexpr int4x2_t operator()(const int32x2_t& x) const
    {
        constexpr auto I0 = Number<0>{};
        constexpr auto I1 = Number<1>{};

        int32_t x0 = vector_type<int32_t, 2>{x}.AsType<int32_t>()[I0];
        int32_t x1 = vector_type<int32_t, 2>{x}.AsType<int32_t>()[I1];

        int8_t high = quant(x0);
        int8_t low  = quant(x1);

        int4x2_t y = (high << 4) + (low & 0b00001111);

        return y;
    }

    float scale_;
};

struct HardTanhQuant
{
    // FIXME: We just need one scale for Relu / Leaky Relu / PRelu
    __host__ __device__ HardTanhQuant(float scaleRelu) : scale_(scaleRelu) {}

    __host__ __device__ constexpr int8_t operator()(const int32_t& x) const
    {
        float gemm_c_float = static_cast<float>(x);
        float hard_tanh    = gemm_c_float > 1 ? 1 : (gemm_c_float < -1 ? -1 : gemm_c_float);
        float tanh_requant = scale_ * hard_tanh;
        int8_t y           = static_cast<int8_t>(
            tanh_requant > 127 ? 127 : tanh_requant < -128 ? -128 : tanh_requant);
        return y;
    }

    //__host__ __device__ constexpr int4x2_t operator()(const int32x2_t& x) const { return 0; }

    float scale_;
};

struct ReluQuant
{
    // FIXME: We just need one scale for Relu / Leaky Relu / PRelu
    __host__ __device__ ReluQuant(float scaleRelu) : scale_(scaleRelu) {}

    __host__ __device__ constexpr int8_t operator()(const int32_t& x) const
    {
        float gemm_c_float = static_cast<float>(x);
        float relu         = gemm_c_float > 0 ? gemm_c_float : 0;
        float relu_requant = scale_ * relu;
        int8_t y           = static_cast<int8_t>(
            relu_requant > 127 ? 127 : relu_requant < -128 ? -128 : relu_requant);
        return y;
    }

    // for reference_gemm
    __host__ __device__ constexpr float operator()(const float& x) const
    {
        float gemm_c_float = x;
        float relu         = gemm_c_float > 0 ? gemm_c_float : 0;
        float relu_requant = scale_ * relu;
        float y            = static_cast<float>(
            relu_requant > 127 ? 127 : relu_requant < -128 ? -128 : relu_requant);
        return y;
    }

    float scale_;
};

} // namespace element_wise
} // namespace tensor_operation
} // namespace ck

namespace ck {
namespace tensor_operation {
namespace element_wise {

struct AddLeakyReluAdd
{
    template <typename T1, typename T2>
    __host__ constexpr float operator()(float v0, T1 v1, T2 v2) const
    {
        float a = v0 + v1;
        float b = 0.1 * a;
        float c = b > 0 ? b : 0;
        float d = c + v2;

        return d;
    }

    template <typename T1, typename T2>
    __device__ constexpr float operator()(float v0, T1 v1, T2 v2) const
    {
#if 0
        // this use not too many registers, but use fp64 mul
        float a = v0 + v1;
        float b = 0.1 * a;
        float c = b > 0 ? b : 0;
        float d = c + v2;

        return d;
#elif 0
        // this spill register
        float a = v0 + v1;
        float b = float(0.1) * a;
        float c = b > 0 ? b : 0;
        float d = c + v2;

        return d;
#elif 0
        // this use lots of registers (but no spill)
        constexpr float alpha     = 0.1;
        constexpr float alpha_inv = 1.0 / alpha;

        float a = v2 * alpha_inv;
        float b = v1 + v0;
        float c = b > 0 ? b : 0;
        float d = alpha * (a + c);

        return d;
#elif 1
        // this use lots of registers (but no spill), 89 Tflops
        constexpr float alpha     = 0.1;
        constexpr float alpha_inv = 1.0 / alpha;

        float a = v2 * alpha_inv;
        float b = v1 + v0;
        float c = max(b, float(0));
        float d = alpha * (a + c);

        return d;
#elif 1
        // this spill registers, 89 Tflops
        float a     = v0 + v1;
        float alpha = 0.1;

        float b;
        asm volatile("\n \
                v_mul_f32_e32 %0, %1, %2 \n \
                "
                     : "=v"(b)
                     : "s"(alpha), "v"(a));

        float c = b > 0 ? b : 0;
        float d = c + v2;

        return d;
#endif
    }
};
} // namespace element_wise
} // namespace tensor_operation
} // namespace ck
#endif
