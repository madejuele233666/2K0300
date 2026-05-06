cmake_minimum_required(VERSION 3.5.0)

include(cross.cmake)

set(CURRENT_DIR ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(NEW_DIR ${CURRENT_DIR}/.. ABSOLUTE)
get_filename_component(REPO_ROOT ${NEW_DIR}/.. ABSOLUTE)
set(TRUE_VENDOR_ROOT ${REPO_ROOT}/true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library)
set(LS2K_LIB_ROOT ${TRUE_VENDOR_ROOT}/libraries)
get_filename_component(PROJ_NAME ${NEW_DIR} NAME)
project(${PROJ_NAME} VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(COMMON_FLAGS "-O3 -march=loongarch64 -mtune=loongarch64 -fPIC -ffunction-sections -fdata-sections -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS} -pthread -Wno-delete-non-virtual-dtor -Wno-placement-new")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

option(LS2K_PERF_ENABLED "Enable fixed-stage runtime performance counters" OFF)
option(LS2K_PERF_USE_CYCLE_COUNTER "Use LoongArch cycle counter for performance counters when available" ON)

set(CMAKE_PREFIX_PATH "/opt/ls_2k0300_env/opencv_4_10_build")
find_package(OpenCV REQUIRED)

include_directories(
    ${OpenCV_INCLUDE_DIRS}
    ../user
    ../code
    ../code/port
    ../code/platform
    ../code/platform/true_ls2k0300
    ../code/legacy
    ../code/runtime
    ${LS2K_LIB_ROOT}/zf_common
    ${LS2K_LIB_ROOT}/zf_device
    ${LS2K_LIB_ROOT}/zf_driver
    ${LS2K_LIB_ROOT}/zf_components/seekfree_assistant
    ${LS2K_LIB_ROOT}/zf_components/ncnn/include
    ${LS2K_LIB_ROOT}/zf_components/tflm
    ${LS2K_LIB_ROOT}/zf_components/tflm/third_party/flatbuffers/include
    ${LS2K_LIB_ROOT}/zf_components/tflm/third_party/gemmlowp
    ${LS2K_LIB_ROOT}/zf_components/tflm/third_party/gemmlowp/fixedpoint
    ${LS2K_LIB_ROOT}/zf_components/tflm/third_party/ruy
    ${LS2K_LIB_ROOT}/zf_components/tflm/third_party/ruy/ruy
    ${LS2K_LIB_ROOT}/zf_components/tflm/tensorflow/lite/micro
    ${LS2K_LIB_ROOT}/zf_components/tflm/tensorflow/lite/micro/kernels
    ${LS2K_LIB_ROOT}/zf_components/tflm/tensorflow/lite/kernels/internal
    ${LS2K_LIB_ROOT}/zf_components/tflm/tensorflow/lite/core/api
    ${LS2K_LIB_ROOT}/zf_components/tflm/tensorflow/lite/core/c
    ${LS2K_LIB_ROOT}/zf_components/tflm/tensorflow/lite/schema
)

set(NEW_SRCS
    ../user/main.cpp
    ../code/platform/bootstrap.cpp
    ../code/platform/camera_adapter.cpp
    ../code/platform/imu_adapter.cpp
    ../code/platform/encoder_adapter.cpp
    ../code/platform/motor_adapter.cpp
    ../code/platform/power_adapter.cpp
    ../code/platform/param_store.cpp
    ../code/platform/hardware_profile.cpp
    ../code/platform/assistant_link.cpp
    ../code/platform/assistant_protocol.cpp
    ../code/platform/steering_media_link.cpp
    ../code/platform/steering_media_protocol.cpp
    ../code/port/perf_counter.cpp
    ../code/platform/true_ls2k0300/camera_bridge.cpp
    ../code/platform/true_ls2k0300/imu_bridge.cpp
    ../code/platform/true_ls2k0300/encoder_bridge.cpp
    ../code/platform/true_ls2k0300/motor_bridge.cpp
    ../code/platform/true_ls2k0300/adc_bridge.cpp
    ../code/platform/true_ls2k0300/timer_bridge.cpp
    ../code/platform/true_ls2k0300/assistant_bridge.cpp
    ../code/platform/true_ls2k0300/steering_media_bridge.cpp
    ../code/legacy/steering_yaw_controller.cpp
    ../code/legacy/motor_logic.cpp
    ../code/legacy/wheel_target_mixer.cpp
    ../code/legacy/wheel_pid.cpp
    ../code/legacy/steering_otsu_threshold.cpp
    ../code/legacy/steering_bev_projector.cpp
    ../code/legacy/steering_bev_simple_perception.cpp
    ../code/legacy/steering_bev_element_raster.cpp
    ../code/legacy/steering_circle_element_evidence.cpp
    ../code/legacy/steering_cross_exit_element_evidence.cpp
    ../code/legacy/steering_visual_element_evidence.cpp
    ../code/legacy/steering_visual_element_pipeline.cpp
    ../code/legacy/steering_visual_reference_orchestration.cpp
    ../code/legacy/steering_reference_usability.cpp
    ../code/legacy/steering_reference_lateral_error.cpp
    ../code/legacy/steering_reference_control_readiness.cpp
    ../code/runtime/startup.cpp
    ../code/runtime/steering_frame_perception_pipeline.cpp
    ../code/runtime/perception_frontend.cpp
    ../code/runtime/low_voltage_sampler.cpp
    ../code/runtime/motion_supervisor.cpp
    ../code/runtime/control_decision.cpp
    ../code/runtime/control_loop.cpp
    ../code/runtime/control_debug_reporter.cpp
    ../code/runtime/assistant_service.cpp
    ../code/runtime/steering_media_service.cpp
    ../code/runtime/tuning_state.cpp
    ../code/runtime/shutdown.cpp
)
set(LS2K_COMMON_SRCS
    ${LS2K_LIB_ROOT}/zf_common/zf_common_fifo.cpp
    ${LS2K_LIB_ROOT}/zf_common/zf_common_font.cpp
    ${LS2K_LIB_ROOT}/zf_common/zf_common_function.cpp
)

set(LS2K_DEVICE_SRCS
    ${LS2K_LIB_ROOT}/zf_device/zf_device_imu_core.cpp
    ${LS2K_LIB_ROOT}/zf_device/zf_device_imu660ra.cpp
    ${LS2K_LIB_ROOT}/zf_device/zf_device_imu660rb.cpp
    ${LS2K_LIB_ROOT}/zf_device/zf_device_imu963ra.cpp
    ${LS2K_LIB_ROOT}/zf_device/zf_device_ips200_fb.cpp
    ${LS2K_LIB_ROOT}/zf_device/zf_device_uvc.cpp
)

set(LS2K_DRIVER_SRCS
    ${LS2K_LIB_ROOT}/zf_driver/zf_driver_adc.cpp
    ${LS2K_LIB_ROOT}/zf_driver/zf_driver_delay.cpp
    ${LS2K_LIB_ROOT}/zf_driver/zf_driver_encoder.cpp
    ${LS2K_LIB_ROOT}/zf_driver/zf_driver_file.cpp
    ${LS2K_LIB_ROOT}/zf_driver/zf_driver_gpio.cpp
    ${LS2K_LIB_ROOT}/zf_driver/zf_driver_pit.cpp
    ${LS2K_LIB_ROOT}/zf_driver/zf_driver_pwm.cpp
    ${LS2K_LIB_ROOT}/zf_driver/zf_driver_udp.cpp
)

set(LS2K_ASSISTANT_SRCS
    ${LS2K_LIB_ROOT}/zf_components/seekfree_assistant/seekfree_assistant.cpp
    ${LS2K_LIB_ROOT}/zf_components/seekfree_assistant/seekfree_assistant_interface.cpp
)

add_executable(
    ${PROJ_NAME}
    ${NEW_SRCS}
    ${LS2K_COMMON_SRCS}
    ${LS2K_DEVICE_SRCS}
    ${LS2K_DRIVER_SRCS}
    ${LS2K_ASSISTANT_SRCS}
)
target_compile_definitions(${PROJ_NAME} PRIVATE
    LS2K_PERF_ENABLED=$<BOOL:${LS2K_PERF_ENABLED}>
    LS2K_PERF_USE_CYCLE_COUNTER=$<BOOL:${LS2K_PERF_USE_CYCLE_COUNTER}>
)
set_target_properties(${PROJ_NAME} PROPERTIES
    INSTALL_RPATH "/opt/ls_2k0300_env/opencv_4_10_build/lib"
    BUILD_WITH_INSTALL_RPATH ON
)

target_link_libraries(
    ${PROJ_NAME}
    ${OpenCV_LIBS}
    pthread
    m
)

option(LS2K_BUILD_VERIFICATION_HELPERS "Build verification helper binaries" OFF)

if(LS2K_BUILD_VERIFICATION_HELPERS)
    add_executable(
        bev_simple_perception_test
        ../verification/tests/bev_simple_perception_test.cpp
        ../code/legacy/steering_bev_projector.cpp
        ../code/legacy/steering_bev_simple_perception.cpp
        ../code/legacy/steering_bev_element_raster.cpp
        ../code/legacy/steering_circle_element_evidence.cpp
        ../code/legacy/steering_visual_element_evidence.cpp
        ../code/legacy/steering_cross_exit_element_evidence.cpp
        ../code/legacy/steering_visual_element_pipeline.cpp
        ../code/legacy/steering_visual_reference_orchestration.cpp
        ../code/legacy/steering_reference_usability.cpp
        ../code/port/perf_counter.cpp
    )
    target_link_libraries(
        bev_simple_perception_test
        pthread
        m
    )

    add_executable(
        reference_usability_lateral_error_test
        ../verification/tests/reference_usability_lateral_error_test.cpp
        ../code/legacy/steering_reference_usability.cpp
        ../code/legacy/steering_reference_lateral_error.cpp
        ../code/legacy/steering_reference_control_readiness.cpp
        ../code/port/perf_counter.cpp
        ../code/legacy/steering_yaw_controller.cpp
        ../code/runtime/control_decision.cpp
    )
    target_link_libraries(
        reference_usability_lateral_error_test
        pthread
        m
    )

    add_executable(
        visual_reference_orchestration_test
        ../verification/tests/visual_reference_orchestration_test.cpp
        ../code/legacy/steering_visual_reference_orchestration.cpp
    )
    target_link_libraries(
        visual_reference_orchestration_test
        pthread
        m
    )

    add_executable(
        visual_element_evidence_test
        ../verification/tests/visual_element_evidence_test.cpp
        ../code/legacy/steering_bev_projector.cpp
        ../code/legacy/steering_bev_simple_perception.cpp
        ../code/legacy/steering_bev_element_raster.cpp
        ../code/legacy/steering_circle_element_evidence.cpp
        ../code/legacy/steering_cross_exit_element_evidence.cpp
        ../code/legacy/steering_visual_element_pipeline.cpp
        ../code/legacy/steering_visual_reference_orchestration.cpp
    )
    target_link_libraries(
        visual_element_evidence_test
        pthread
        m
    )

    add_executable(
        assistant_telemetry_selftest
        ../verification/tests/assistant_telemetry_selftest.cpp
        ../code/platform/assistant_protocol.cpp
        ../code/runtime/motion_supervisor.cpp
    )
    target_link_libraries(
        assistant_telemetry_selftest
        ${OpenCV_LIBS}
        pthread
        m
    )

    add_executable(
        runtime_parameter_defaults_test
        ../verification/tests/runtime_parameter_defaults_test.cpp
    )
    target_link_libraries(
        runtime_parameter_defaults_test
        pthread
        m
    )

    add_executable(
        param_store_load_runtime_parameters_test
        ../verification/tests/param_store_load_runtime_parameters_test.cpp
        ../code/platform/param_store.cpp
    )
    target_link_libraries(
        param_store_load_runtime_parameters_test
        ${OpenCV_LIBS}
        pthread
        m
    )

    add_executable(
        power_adapter_threshold_test
        ../verification/tests/power_adapter_threshold_test.cpp
        ../code/platform/power_adapter.cpp
        ../code/platform/true_ls2k0300/adc_bridge.cpp
    )
    target_link_libraries(
        power_adapter_threshold_test
        pthread
        m
    )

    add_executable(
        startup_low_voltage_order_test
        ../verification/tests/startup_low_voltage_order_test.cpp
        ../code/runtime/startup.cpp
        ../code/platform/hardware_profile.cpp
    )
    target_link_libraries(
        startup_low_voltage_order_test
        pthread
        m
    )

    add_executable(
        steering_media_selftest
        ../user/steering_media_selftest.cpp
        ../code/platform/steering_media_link.cpp
        ../code/platform/steering_media_protocol.cpp
        ../code/platform/true_ls2k0300/steering_media_bridge.cpp
        ../code/runtime/control_debug_reporter.cpp
        ../code/runtime/control_decision.cpp
        ../code/runtime/motion_supervisor.cpp
        ../code/runtime/steering_media_service.cpp
        ../code/port/perf_counter.cpp
    )
    target_link_libraries(
        steering_media_selftest
        pthread
        m
    )

    add_executable(
        scene_overlay_probe
        ../user/scene_overlay_probe.cpp
        ../code/legacy/steering_otsu_threshold.cpp
        ../code/legacy/steering_bev_projector.cpp
        ../code/legacy/steering_bev_simple_perception.cpp
        ../code/legacy/steering_bev_element_raster.cpp
        ../code/legacy/steering_circle_element_evidence.cpp
        ../code/legacy/steering_cross_exit_element_evidence.cpp
        ../code/legacy/steering_visual_element_pipeline.cpp
        ../code/legacy/steering_visual_reference_orchestration.cpp
        ../code/legacy/steering_reference_usability.cpp
        ../code/legacy/steering_reference_lateral_error.cpp
        ../code/legacy/steering_reference_control_readiness.cpp
        ../code/port/perf_counter.cpp
    )
    target_link_libraries(
        scene_overlay_probe
        pthread
        m
    )
endif()
