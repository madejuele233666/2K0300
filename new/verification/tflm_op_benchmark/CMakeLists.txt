cmake_minimum_required(VERSION 3.5.0)

set(LS2K_TFLM_ROOT "" CACHE PATH "Path to the LS2K TFLM source tree")
if(NOT LS2K_TFLM_ROOT)
    message(FATAL_ERROR "LS2K_TFLM_ROOT is required")
endif()
get_filename_component(LS2K_TFLM_ROOT "${LS2K_TFLM_ROOT}" ABSOLUTE)

include(${LS2K_TFLM_ROOT}/cross.cmake)

project(tflm_op_benchmark_static_lib LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wno-delete-non-virtual-dtor -Wno-placement-new")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=loongarch64 -mtune=loongarch64 -fPIC -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=loongarch64 -mtune=loongarch64 -fPIC -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

set(TFLM_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib" CACHE PATH "Output directory for libtflm.a")

include_directories(${LS2K_TFLM_ROOT})
include_directories(${LS2K_TFLM_ROOT}/third_party/flatbuffers/include)
include_directories(${LS2K_TFLM_ROOT}/third_party/gemmlowp)
include_directories(${LS2K_TFLM_ROOT}/third_party/gemmlowp/fixedpoint)
include_directories(${LS2K_TFLM_ROOT}/third_party/ruy)
include_directories(${LS2K_TFLM_ROOT}/third_party/ruy/ruy)
include_directories(${LS2K_TFLM_ROOT}/tensorflow/lite/micro)
include_directories(${LS2K_TFLM_ROOT}/tensorflow/lite/micro/kernels)
include_directories(${LS2K_TFLM_ROOT}/tensorflow/lite/kernels/internal)
include_directories(${LS2K_TFLM_ROOT}/tensorflow/lite/core/api)
include_directories(${LS2K_TFLM_ROOT}/tensorflow/lite/core/c)
include_directories(${LS2K_TFLM_ROOT}/tensorflow/lite/schema)

set(TFLM_OP_BENCH_INCLUDE_OVERLAY ${CMAKE_CURRENT_BINARY_DIR}/op_bench_include)
file(MAKE_DIRECTORY ${TFLM_OP_BENCH_INCLUDE_OVERLAY}/tensorflow/lite/micro/kernels)
set(TFLM_OP_BENCH_UNUSED_HEADERS
    hard_swish.h
    leaky_relu.h
    logistic.h
    sub.h
)
foreach(TFLM_OP_BENCH_UNUSED_HEADER IN LISTS TFLM_OP_BENCH_UNUSED_HEADERS)
    configure_file(
        ${LS2K_TFLM_ROOT}/tensorflow/lite/micro/kernels/unused/${TFLM_OP_BENCH_UNUSED_HEADER}
        ${TFLM_OP_BENCH_INCLUDE_OVERLAY}/tensorflow/lite/micro/kernels/${TFLM_OP_BENCH_UNUSED_HEADER}
        COPYONLY
    )
endforeach()
include_directories(${TFLM_OP_BENCH_INCLUDE_OVERLAY})

aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/compiler/mlir/lite/core/api DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/compiler/mlir/lite/schema DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/lite/core/api DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/lite/core/c DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/lite/kernels DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/lite/kernels/internal DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/lite/micro DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/lite/micro/cortex_m_generic DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/lite/micro/arena_allocator DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/lite/micro/memory_planner DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/lite/micro/tflite_bridge DIR_TFLM_SRCS)
aux_source_directory(${LS2K_TFLM_ROOT}/tensorflow/lite/micro/kernels DIR_TFLM_SRCS)

set(TFLM_OP_BENCH_UNUSED_SRCS
    add_n.cc
    arg_min_max.cc
    batch_to_space_nd.cc
    broadcast_args.cc
    broadcast_to.cc
    depth_to_space.cc
    div.cc
    elu.cc
    fill.cc
    gather.cc
    gather_nd.cc
    hard_swish.cc
    hard_swish_common.cc
    l2_pool_2d.cc
    l2norm.cc
    leaky_relu.cc
    leaky_relu_common.cc
    log_softmax.cc
    logistic.cc
    logistic_common.cc
    mirror_pad.cc
    reduce.cc
    reduce_common.cc
    select.cc
    space_to_batch_nd.cc
    space_to_depth.cc
    split.cc
    split_v.cc
    sub.cc
    sub_common.cc
    tanh.cc
    unpack.cc
    zeros_like.cc
)
foreach(TFLM_OP_BENCH_UNUSED_SRC IN LISTS TFLM_OP_BENCH_UNUSED_SRCS)
    list(APPEND DIR_TFLM_SRCS ${LS2K_TFLM_ROOT}/tensorflow/lite/micro/kernels/unused/${TFLM_OP_BENCH_UNUSED_SRC})
endforeach()

add_library(tflm STATIC ${DIR_TFLM_SRCS})
set_target_properties(tflm PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${TFLM_ARCHIVE_OUTPUT_DIRECTORY}
    OUTPUT_NAME "tflm"
    CLEAN_DIRECT_OUTPUT 1
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 99
)
target_link_libraries(tflm pthread m)
