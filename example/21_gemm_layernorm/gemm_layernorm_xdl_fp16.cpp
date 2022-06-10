#include <iostream>
#include <numeric>
#include <initializer_list>
#include <cstdlib>
#include <stdlib.h>

#include "check_err.hpp"
#include "config.hpp"
#include "device.hpp"
#include "host_tensor.hpp"
#include "host_tensor_generator.hpp"
#include "device_tensor.hpp"
#include "device_5ary_elementwise.hpp"
#include "device_gemm_reduce_xdl_cshuffle.hpp"
#include "element_wise_operation.hpp"
#include "reference_gemm.hpp"
#include "gemm_specialization.hpp"
#include "element_wise_reduce_operation.hpp"

template <ck::index_t... Is>
using S = ck::Sequence<Is...>;

using F16 = ck::half_t;
using F32 = float;

using Row = ck::tensor_layout::gemm::RowMajor;
using Col = ck::tensor_layout::gemm::ColumnMajor;

using ADataType                = F16;
using BDataType                = F16;
using CDataType                = F16;
using GemmAccDataType          = F32;
using ReduceAccDataType        = F32;
using DDataType                = F32;
using DPtrsGlobal              = ck::Tuple<DDataType*, DDataType*>;
using GammaDataType            = F16;
using BetaDataType             = F16;
using LayerNormOutDataType     = F16;
using NormalizeComputeDataType = F32;

using ALayout = ck::tensor_layout::gemm::RowMajor;
using BLayout = ck::tensor_layout::gemm::ColumnMajor;
using CLayout = ck::tensor_layout::gemm::RowMajor;

using AElementOp  = ck::tensor_operation::element_wise::PassThrough;
using BElementOp  = ck::tensor_operation::element_wise::PassThrough;
using CElementOp  = ck::tensor_operation::element_wise::PassThrough;
using ReduceSumOp = ck::reduce::Add;
using DxsReduceOp = ck::Tuple<ReduceSumOp, ReduceSumOp>;

using UnaryIdenticElementOp = ck::tensor_operation::element_wise::PassThrough;
using UnaryDivElementOp     = ck::tensor_operation::element_wise::UnaryDivide;
using UnarySquareElementOp  = ck::tensor_operation::element_wise::UnarySquare;
using DxsInElementOp        = ck::Tuple<UnaryIdenticElementOp, UnarySquareElementOp>;
using DxsOutElementOp       = ck::Tuple<UnaryDivElementOp, UnaryDivElementOp>;

using DxsGlobalMemOp =
    ck::InMemoryDataOperationEnumSequence<ck::InMemoryDataOperationEnum::AtomicAdd,
                                          ck::InMemoryDataOperationEnum::AtomicAdd>;

static constexpr auto GemmSpecialization =
    ck::tensor_operation::device::GemmSpecialization::Default;

// clang-format off
using DeviceGemmReduceInstance = ck::tensor_operation::device::DeviceGemmReduce_Xdl_CShuffle
//######| ALayout| BLayout| CLayout|AData| BData| CData|  GemmAcc| CShuffle| ReduceAcc|         DData|           A|           B|           C|         Dxs|     DxsInEleOp|     DxsAccEleOp|             D|               GEMM| NumGemmK| Block|  MPer|  NPer|  KPer| AK1| BK1| MPer| NPer| MXdl| NXdl|  ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockLds|  BBlockTransfer| BBlockTransfer| BBlockTransfer| BlockTransfer| BBlockTransfer| BBlockTransfer| BBlockLds|    CShuffle|    CShuffle| CBlockTransferClusterLengths|  CBlockTransfer|              CReduce| CReduceThreadLds2VGprCopy| CReduceThreadVgpr2GlobalCopy|
//######|        |        |        | Type|  Type|  Type| DataType| DataType|  DataType|    Type Tuple| Elementwise| Elementwise| Elementwise|      Reduce|               |                |    MemoryData|     Spacialization| Prefetch|  Size| Block| Block| Block|    |    |  XDL|  XDL|  Per|  Per|   ThreadCluster|  ThreadCluster| SrcAccessOrder|   SrcVectorDim|      SrcScalar|      DstScalar|    ExtraM|   ThreadCluster|  ThreadCluster| SrcAccessOrder|  SrcVectorDim|      SrcScalar|      DstScalar|    ExtraN| MXdlPerWave| NXdlPerWave|            _MBlock_MPerBlock| ScalarPerVector| ThreadClusterLengths|     SrcDstScalarPerVector|        SrcDstScalarPerVector|
//######|        |        |        |     |      |      |         |         |          |              |   Operation|   Operation|   Operation|   Operation|               |                |     Operation|                   |    Stage|      |      |      |      |    |    |     |     | Wave| Wave| Lengths_K0_M_K1|   ArrangeOrder|               |               |      PerVector|   PerVector_K1|          | Lengths_K0_N_K1|   ArrangeOrder|               |              |      PerVector|   PerVector_K1|          |  PerShuffle|  PerShuffle|            _NBlock_NPerBlock|      _NPerBlock| _MPerBlock_NPerBlock|                _NPerBlock|                   _MPerBlock|
//######|        |        |        |     |      |      |         |         |          |              |            |            |            |            |               |                |              |                   |         |      |      |      |      |    |    |     |     |     |     |                |               |               |               |               |               |          |                |               |               |              |               |               |          |            |            |                             |                |                     |                          |                             |
        <     Row,     Col,     Row,  F16,   F16,   F16,      F32,      F32,       F32,   DPtrsGlobal,  AElementOp,  BElementOp,  CElementOp, DxsReduceOp, DxsInElementOp, DxsOutElementOp,  DxsGlobalMemOp, GemmSpecialization,        1,   256,   256,   128,    32,   8,   8,   32,   32,    4,    2,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              8,              8,         1,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,             2,              8,              8,         1,           1,           1,               S<1, 32, 1, 8>,               8,             S<64, 4>,                         4,                            1>;
// clang-format on

using ReferenceGemmInstance = ck::tensor_operation::host::ReferenceGemm<ADataType,
                                                                        BDataType,
                                                                        CDataType,
                                                                        GemmAccDataType,
                                                                        AElementOp,
                                                                        BElementOp,
                                                                        CElementOp>;

using NormalizeFunctor = ck::tensor_operation::element_wise::Normalize;

// A:x, B:E[x], C:E[x^2], D:Gamma, E:Beta , F:y
using DeviceNormalizeInstance =
    ck::tensor_operation::device::Device5AryElementwise<CDataType,
                                                        DDataType,
                                                        DDataType,
                                                        GammaDataType,
                                                        BetaDataType,
                                                        LayerNormOutDataType,
                                                        NormalizeComputeDataType,
                                                        NormalizeFunctor,
                                                        2,
                                                        8,
                                                        8,  // scalarPerVector: gemm_out
                                                        1,  // scalarPerVector: reduce_mean
                                                        1,  // scalarPerVector: reduce_mean_square
                                                        8,  // scalarPerVector: Gamma
                                                        8,  // scalarPerVector: Beta
                                                        8>; // scalarPerVector: LayerNorm_out

auto f_host_tensor_descriptor1d = [](std::size_t len, std::size_t stride) {
    return HostTensorDescriptor(std::vector<std::size_t>({len}),
                                std::vector<std::size_t>({stride}));
};

auto f_host_tensor_descriptor2d =
    [](std::size_t row, std::size_t col, std::size_t stride, auto layout) {
        if(std::is_same<decltype(layout), ck::tensor_layout::gemm::RowMajor>::value)
        {
            return HostTensorDescriptor(std::vector<std::size_t>({row, col}),
                                        std::vector<std::size_t>({stride, 1}));
        }
        else
        {
            return HostTensorDescriptor(std::vector<std::size_t>({row, col}),
                                        std::vector<std::size_t>({1, stride}));
        }
    };

template <typename CDataType,
          typename DDataType,
          typename A_functor,
          typename B_functor,
          typename C_functor>
void host_gemm_layernorm(Tensor<LayerNormOutDataType>& out_m_n,
                         const Tensor<ADataType>& a_m_k,
                         const Tensor<ADataType>& b_k_n,
                         const Tensor<GammaDataType>& gamma_n,
                         const Tensor<GammaDataType>& beta_n,
                         A_functor a_element_op,
                         B_functor b_element_op,
                         C_functor c_element_op,
                         int M,
                         int N)
{
    using out_type = ck::remove_reference_t<decltype(out_m_n(0, 0))>;

    int StrideC = N;
    Tensor<CDataType> c_m_n(f_host_tensor_descriptor2d(M, N, StrideC, CLayout{}));
    Tensor<DDataType> mean_m(f_host_tensor_descriptor1d(M, 1));
    Tensor<DDataType> meanSquare_m(f_host_tensor_descriptor1d(M, 1));
    auto averageOpInst = UnaryDivElementOp{M};

    auto ref_gemm    = ReferenceGemmInstance{};
    auto ref_invoker = ref_gemm.MakeInvoker();

    auto ref_argument =
        ref_gemm.MakeArgument(a_m_k, b_k_n, c_m_n, a_element_op, b_element_op, c_element_op);

    ref_invoker.Run(ref_argument);

    // reduce_mean and reduce_square_mean
    auto reduceSumOpInst = ReduceSumOp{};
    for(int m = 0; m < M; ++m)
    {
        auto mean_acc        = reduceSumOpInst.GetIdentityValue<ReduceAccDataType>();
        auto square_mean_acc = reduceSumOpInst.GetIdentityValue<ReduceAccDataType>();

        for(int n = 0; n < N; ++n)
        {
            auto c_val        = ck::type_convert<ReduceAccDataType>(c_m_n(m, n));
            auto square_c_val = reduceSumOpInst.GetIdentityValue<ReduceAccDataType>();

            UnarySquareElementOp{}(square_c_val, c_val);

            reduceSumOpInst(mean_acc, c_val);
            reduceSumOpInst(square_mean_acc, square_c_val);
        }

        averageOpInst(mean_acc, mean_acc);
        averageOpInst(square_mean_acc, square_mean_acc);
        mean_m(m)       = ck::type_convert<DDataType>(mean_acc);
        meanSquare_m(m) = ck::type_convert<DDataType>(square_mean_acc);
    }

    // LayerNorm
    auto layerNormInst = NormalizeFunctor{};
    for(int m = 0; m < M; ++m)
    {
        for(int n = 0; n < N; ++n)
        {
            float out_f32 = 0;
            layerNormInst(out_f32,
                          static_cast<float>(c_m_n(m, n)),
                          static_cast<float>(mean_m(m)),
                          static_cast<float>(meanSquare_m(m)),
                          static_cast<float>(gamma_n(n)),
                          static_cast<float>(beta_n(n)));
            out_m_n(m, n) = static_cast<out_type>(out_f32);
        }
    }
}

template <typename ADataType,
          typename BDataType,
          typename CDataType,
          typename DDataType,
          typename GammaDataType,
          typename BetaDataType,
          typename NormalizeDataType>
void DumpGemmLayerNormPerf(float gemm_reduce_time, float normalize_time, int M, int N, int K)
{
    std::size_t gemm_flop     = std::size_t(2) * M * N * K;
    std::size_t gemm_num_byte = sizeof(ADataType) * M * K + sizeof(BDataType) * K * N +
                                sizeof(CDataType) * M * N + sizeof(DDataType) * M +
                                sizeof(DDataType) * M;

    std::size_t normalize_num_btye = sizeof(CDataType) * M * N + sizeof(DDataType) * M +
                                     sizeof(DDataType) * M + sizeof(GammaDataType) * N +
                                     sizeof(BetaDataType) * N + sizeof(NormalizeDataType) * M * N;

    float tflops               = static_cast<float>(gemm_flop) / 1.E9 / gemm_reduce_time;
    float gemm_gb_per_sec      = gemm_num_byte / 1.E6 / gemm_reduce_time;
    float normalize_gb_per_sec = normalize_num_btye / 1.E6 / normalize_time;

    std::cout << "gemm + reduce_mean + reduce_square_mean Perf: " << gemm_reduce_time << " ms, "
              << tflops << " TFlops, " << gemm_gb_per_sec << " GB/s, " << std::endl;

    std::cout << "5-ary elementwise Perf: " << normalize_time << " ms, " << normalize_gb_per_sec
              << " GB/s, " << std::endl;
}

int main()
{
    // GEMM shape
    ck::index_t M = 1024;
    ck::index_t N = 1024;
    ck::index_t K = 1024;

    ck::index_t StrideA = 1024;
    ck::index_t StrideB = 1024;
    ck::index_t StrideC = 1024;

    Tensor<ADataType> a_m_k(f_host_tensor_descriptor2d(M, K, StrideA, ALayout{}));
    Tensor<BDataType> b_k_n(f_host_tensor_descriptor2d(K, N, StrideB, BLayout{}));
    Tensor<CDataType> c_m_n(f_host_tensor_descriptor2d(M, N, StrideC, CLayout{}));
    Tensor<DDataType> reduceMean_m(f_host_tensor_descriptor1d(M, 1));
    Tensor<DDataType> reduceMeanSquare_m(f_host_tensor_descriptor1d(M, 1));
    Tensor<GammaDataType> gamma_n(f_host_tensor_descriptor1d(N, 1));
    Tensor<BetaDataType> beta_n(f_host_tensor_descriptor1d(N, 1));
    Tensor<LayerNormOutDataType> layerNorm_m_n(
        f_host_tensor_descriptor2d(M, N, StrideC, CLayout{}));

    a_m_k.GenerateTensorValue(GeneratorTensor_3<ADataType>{-1, 1});
    b_k_n.GenerateTensorValue(GeneratorTensor_3<BDataType>{-1, 1});
    gamma_n.GenerateTensorValue(GeneratorTensor_3<GammaDataType>{-1, 1});
    beta_n.GenerateTensorValue(GeneratorTensor_3<BetaDataType>{-1, 1});

    DeviceMem a_device_buf(sizeof(ADataType) * a_m_k.mDesc.GetElementSpace());
    DeviceMem b_device_buf(sizeof(BDataType) * b_k_n.mDesc.GetElementSpace());
    DeviceMem c_device_buf(sizeof(CDataType) * c_m_n.mDesc.GetElementSpace());
    DeviceMem reduceMean_device_buf(sizeof(DDataType) * reduceMean_m.mDesc.GetElementSpace());
    DeviceMem reduceMeanSquare_device_buf(sizeof(DDataType) *
                                          reduceMeanSquare_m.mDesc.GetElementSpace());
    DeviceMem gamma_device_buf(sizeof(GammaDataType) * gamma_n.mDesc.GetElementSpace());
    DeviceMem beta_device_buf(sizeof(BetaDataType) * beta_n.mDesc.GetElementSpace());
    DeviceMem layerNorm_device_buf(sizeof(LayerNormOutDataType) *
                                   layerNorm_m_n.mDesc.GetElementSpace());

    a_device_buf.ToDevice(a_m_k.mData.data());
    b_device_buf.ToDevice(b_k_n.mData.data());
    gamma_device_buf.ToDevice(gamma_n.mData.data());
    beta_device_buf.ToDevice(beta_n.mData.data());

    auto a_element_op = AElementOp{};
    auto b_element_op = BElementOp{};
    auto c_element_op = CElementOp{};
    auto dxs_global =
        ck::make_tuple(static_cast<DDataType*>(reduceMean_device_buf.GetDeviceBuffer()),
                       static_cast<DDataType*>(reduceMeanSquare_device_buf.GetDeviceBuffer()));

    auto dxs_in_element_op  = DxsInElementOp{};
    auto dxs_out_element_op = DxsOutElementOp{M, M};

    // Prepare GEMM, reduce_mean, reduce_mean_square
    auto gemmReduce         = DeviceGemmReduceInstance{};
    auto gemmReduce_invoker = gemmReduce.MakeInvoker();
    auto gemmReduce_argument =
        gemmReduce.MakeArgument(static_cast<ADataType*>(a_device_buf.GetDeviceBuffer()),
                                static_cast<BDataType*>(b_device_buf.GetDeviceBuffer()),
                                static_cast<CDataType*>(c_device_buf.GetDeviceBuffer()),
                                dxs_global,
                                M,
                                N,
                                K,
                                StrideA,
                                StrideB,
                                StrideC,
                                a_element_op,
                                b_element_op,
                                c_element_op,
                                dxs_in_element_op,
                                dxs_out_element_op);

    if(!gemmReduce.IsSupportedArgument(gemmReduce_argument))
    {
        throw std::runtime_error(
            "wrong! device_gemm with the specified compilation parameters does "
            "not support this GEMM problem");
    }

    reduceMean_device_buf.SetZero();
    reduceMeanSquare_device_buf.SetZero();

    // Prepare LayerNorm
    auto normalize          = DeviceNormalizeInstance{};
    auto normalize_invoker  = normalize.MakeInvoker();
    auto normalize_argument = normalize.MakeArgument(
        static_cast<CDataType*>(c_device_buf.GetDeviceBuffer()),
        static_cast<DDataType*>(reduceMean_device_buf.GetDeviceBuffer()),
        static_cast<DDataType*>(reduceMeanSquare_device_buf.GetDeviceBuffer()),
        static_cast<GammaDataType*>(gamma_device_buf.GetDeviceBuffer()),
        static_cast<BetaDataType*>(beta_device_buf.GetDeviceBuffer()),
        static_cast<LayerNormOutDataType*>(layerNorm_device_buf.GetDeviceBuffer()),
        {M, N},
        {StrideC, 1},
        {1, 0},
        {1, 0},
        {0, 1},
        {0, 1},
        {StrideC, 1},
        NormalizeFunctor{});

    if(!normalize.IsSupportedArgument(normalize_argument))
    {
        throw std::runtime_error("The runtime parameters seems not supported by the "
                                 "Device5AryElementwise instance, exiting!");
    }

    // run kernel
    gemmReduce_invoker.Run(gemmReduce_argument, StreamConfig{nullptr, false});
    normalize_invoker.Run(normalize_argument, StreamConfig{nullptr, false});

    bool pass = true;
    {
        // verification
        Tensor<LayerNormOutDataType> host_layerNorm_m_n(
            f_host_tensor_descriptor2d(M, N, StrideC, CLayout{}));

        host_gemm_layernorm<CDataType, DDataType>(host_layerNorm_m_n,
                                                  a_m_k,
                                                  b_k_n,
                                                  gamma_n,
                                                  beta_n,
                                                  a_element_op,
                                                  b_element_op,
                                                  c_element_op,
                                                  M,
                                                  N);

        layerNorm_device_buf.FromDevice(layerNorm_m_n.mData.data());
        pass &= ck::utils::check_err(layerNorm_m_n.mData,
                                     host_layerNorm_m_n.mData,
                                     "Error: Incorrect results d1",
                                     1e-3,
                                     1e-3);
    }

    {
        // evaluate kernel perf
        bool time_kernel = true;

        float gemm_reduce_mean_reduce_square_mean_ave_time =
            gemmReduce_invoker.Run(gemmReduce_argument, StreamConfig{nullptr, time_kernel});
        float normalize_ave_time =
            normalize_invoker.Run(normalize_argument, StreamConfig{nullptr, time_kernel});

        if(time_kernel)
            DumpGemmLayerNormPerf<ADataType,
                                  BDataType,
                                  CDataType,
                                  DDataType,
                                  GammaDataType,
                                  BetaDataType,
                                  LayerNormOutDataType>(
                gemm_reduce_mean_reduce_square_mean_ave_time, normalize_ave_time, M, N, K);
    }

    return pass ? 0 : 1;
}
