/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#pragma once

#include <array>
#include <string>

#include "ngraph/codegen/code_writer.hpp"
#include "ngraph/coordinate.hpp"
#include "ngraph/runtime/gpu/gpu_cuda_kernel_builder.hpp"
#include "ngraph/runtime/gpu/gpu_runtime_context.hpp"
#include "ngraph/strides.hpp"

namespace ngraph
{
    namespace runtime
    {
        namespace gpu
        {
            template <typename T>
            struct CudaOpMap;

            void emit_broadcast(const std::string& name,
                                std::array<std::string, 2> data_types,
                                GPURuntimeContext* ctx,
                                CUdeviceptr in,
                                CUdeviceptr out,
                                size_t repeat_size,
                                size_t repeat_times,
                                size_t count);

            void emit_onehot(const std::string& name,
                             std::array<std::string, 2> data_types,
                             GPURuntimeContext* ctx,
                             CUdeviceptr in,
                             CUdeviceptr out,
                             size_t repeat_size,
                             size_t repeat_times,
                             size_t count);

            template <typename T, typename... Inputs>
            void emit_elementwise_op(const std::string& name,
                                     const std::array<std::string, 2>& data_types,
                                     GPURuntimeContext* ctx,
                                     size_t count,
                                     CUdeviceptr out,
                                     Inputs&&... inputs)
            {
                std::string type_signature = "_" + data_types[0] + "_" + data_types[1];
                std::replace(type_signature.begin(), type_signature.end(), ' ', '_');
                auto compiled_kernel = ctx->nvrtc_cache->get(name + type_signature);
                if (compiled_kernel == nullptr)
                {
                    codegen::CodeWriter writer;
                    CudaKernelBuilder::add_pod_typedefs(writer);

                    std::string op_name = CudaOpMap<T>::op;
                    if (CudaOpMap<T>::math_kernel)
                    {
                        op_name += type_signature;
                        CudaKernelBuilder::get_device_helper(writer,
                                                             op_name,
                                                             CudaOpMap<T>::math_kernel,
                                                             data_types,
                                                             sizeof...(inputs));
                    }

                    CudaKernelBuilder::get_elementwise_op(
                        writer, name + type_signature, op_name, data_types, sizeof...(inputs));

                    std::string kernel = writer.get_code();
                    compiled_kernel = ctx->nvrtc_cache->set(name + type_signature, kernel);
                }

                //convert runtime ptr to driver api ptr
                void* args_list[] = {&inputs..., &out, &count};
                CUDA_SAFE_CALL(cuLaunchKernel(*compiled_kernel.get(),
                                              count,
                                              1,
                                              1, // grid dim
                                              1,
                                              1,
                                              1, // block dim
                                              0,
                                              NULL, // shared mem and stream
                                              args_list,
                                              0));  // arguments
                CUDA_SAFE_CALL(cuCtxSynchronize()); // Retrieve and print output.
            }

            void emit_reshape(const std::string& name,
                              const std::array<std::string, 2>& data_types,
                              GPURuntimeContext* ctx,
                              CUdeviceptr in,
                              CUdeviceptr out,
                              CUdeviceptr input_strides,
                              CUdeviceptr trans_strides,
                              size_t rank,
                              size_t count);

            template <typename... Args>
            void emit_1d_max_pool(GPURuntimeContext* ctx,
                                  const std::string& name,
                                  const std::array<std::string, 2>& data_types,
                                  size_t count,
                                  Args&&... args)
            {
                std::string name_signature = name + "_" + data_types[0] + "_" + data_types[1];
                std::replace(name_signature.begin(), name_signature.end(), ' ', '_');
                auto compiled_kernel = ctx->nvrtc_cache->get(name_signature);
                if (compiled_kernel == nullptr)
                {
                    codegen::CodeWriter writer;
                    CudaKernelBuilder::get_1d_max_pool(writer, name_signature, data_types);
                    std::string kernel = writer.get_code();
                    compiled_kernel = ctx->nvrtc_cache->set(name_signature, kernel);
                }

                if (sizeof...(args))
                {
                    std::vector<void*> args_list = {&args..., &count};
                    CUDA_SAFE_CALL(cuLaunchKernel(*compiled_kernel.get(),
                                                  static_cast<unsigned int>(count),
                                                  1,
                                                  1, // grid dim
                                                  1,
                                                  1,
                                                  1, // block dim
                                                  0,
                                                  NULL, // shared mem and stream
                                                  &args_list[0],
                                                  0));  // arguments
                    CUDA_SAFE_CALL(cuCtxSynchronize()); // Retrieve and print output.
                }
            }
        }
    }
}
