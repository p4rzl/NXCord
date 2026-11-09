cmake_minimum_required(VERSION 3.1)

project(stratosphere)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(stratosphere STATIC
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/pm/pm_shell_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/pm/pm_dmnt_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/pm/pm_boot_mode_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/pm/pm_info_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/settings/settings_fwdbg_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/spl/spl_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/spl/smc/spl_smc.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/ams/ams_environment.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/ams/ams_exosphere_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/ams/ams_emummc_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sm/sm_mitm_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sm/sm_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sm/sm_manager_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sm/sm_utils.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/diag/diag_assertion_impl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/result/result_on_assertion.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/lmem/impl/lmem_impl_exp_heap.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/lmem/impl/lmem_impl_unit_heap.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/lmem/impl/lmem_impl_common_heap.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/lmem/lmem_unit_heap.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/lmem/lmem_exp_heap.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/lmem/lmem_common.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sf/cmif/sf_cmif_service_dispatch.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sf/cmif/sf_cmif_domain_manager.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sf/cmif/sf_cmif_service_object_holder.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sf/cmif/sf_cmif_domain_service_object.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sf/hipc/sf_hipc_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sf/hipc/sf_hipc_server_manager.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sf/hipc/sf_hipc_server_session_manager.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sf/hipc/sf_hipc_mitm_query_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/sf/hipc/sf_hipc_server_domain_session_manager.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/hos/hos_version_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/impl/os_rng_manager_impl.os.horizon.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/impl/os_rng_manager_impl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/impl/os_inter_process_event.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/impl/os_tick_manager_impl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/impl/os_waitable_manager_impl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/impl/os_resource_manager.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/impl/os_random_impl.os.horizon.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/impl/os_memory_permission_impl.os.horizon.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_interrupt_event.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_random.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_waitable_holder.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_event.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_memory_heap.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_virtual_address_memory.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_tick.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_waitable_manager.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_memory_permission.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_process_handle.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_semaphore.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_thread_local_storage_api.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_message_queue.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/os/os_system_event.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/mem/impl/mem_impl_platform.os.horizon.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/mem/impl/heap/mem_impl_heap_tls_heap_central.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/mem/impl/heap/mem_impl_heap_tls_heap_cache.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/mem/impl/heap/mem_impl_heap_central_heap.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/mem/impl/heap/mem_impl_heap_cached_heap.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/source/mem/mem_standard_allocator.cpp)

target_compile_definitions(stratosphere
        PUBLIC
        ATMOSPHERE_IS_STRATOSPHERE ATMOSPHERE_BOARD_NINTENDO_SWITCH ATMOSPHERE_OS_HORIZON ATMOSPHERE_BOARD_NINTENDO_NX
        ATMOSPHERE_ARCH_ARM64 _GNU_SOURCE)
target_include_directories(stratosphere
        PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libstratosphere/include
        ${CMAKE_CURRENT_LIST_DIR}/Atmosphere-libs/libvapours/include)
target_link_libraries(stratosphere switch::libnx)
