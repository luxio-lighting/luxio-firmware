# The SDK's heap-backed pools normally ignore their capacity argument. Keep
# the original bounds while allocating objects on demand. All Matter consumers
# must compile against this same header; never mix with precompiled archives.
idf_component_get_property(matter_dir espressif__esp_matter COMPONENT_DIR)
set(pool_header "${matter_dir}/connectedhomeip/connectedhomeip/src/lib/support/Pool.h")
file(SHA256 "${pool_header}" pool_hash)
if(NOT pool_hash STREQUAL "0679281a4faecd884f37b5a18e21de57646e63e8b8117eaac09d327eb5a166e1")
    message(FATAL_ERROR "Matter pool header changed; review the bounded allocator before updating the SDK")
endif()
file(READ "${pool_header}" pool_source)
set(original "class ObjectPool<T, N, ObjectPoolMem::kHeap> : public HeapObjectPool<T>\n{\n};")
set(bounded "class ObjectPool<T, N, ObjectPoolMem::kHeap> : public HeapObjectPool<T>
{
public:
    template <class... Args> T * CreateObject(Args &&... args)
    {
        if (this->Allocated() >= N) return nullptr;
        return HeapObjectPool<T>::CreateObject(std::forward<Args>(args)...);
    }
};")
string(FIND "${pool_source}" "${original}" pool_offset)
if(pool_offset LESS 0)
    message(FATAL_ERROR "Cannot locate the expected Matter heap pool specialization")
endif()
string(REPLACE "${original}" "${bounded}" pool_source "${pool_source}")
set(pool_overlay "${CMAKE_BINARY_DIR}/luxio-include")
file(MAKE_DIRECTORY "${pool_overlay}/lib/support")
file(WRITE "${pool_overlay}/lib/support/Pool.h" "${pool_source}")
foreach(component espressif__esp_matter arduino main)
    idf_component_get_property(component_lib ${component} COMPONENT_LIB)
    target_include_directories(${component_lib} BEFORE PUBLIC "${pool_overlay}")
endforeach()

# Espressif hard-codes this in its generated build header. Keep fabric quotas
# enabled alongside the bounded heap pools, independent of header include order.
set(app_config "${CMAKE_BINARY_DIR}/esp-idf/espressif__esp_matter/app/AppBuildConfig.h")
file(READ "${app_config}" app_source)
string(FIND "${app_source}" "#define CHIP_CONFIG_IM_FORCE_FABRIC_QUOTA_CHECK 0" quota_offset)
if(quota_offset LESS 0)
    message(FATAL_ERROR "Matter quota configuration changed; review before updating the SDK")
endif()
string(REPLACE "#define CHIP_CONFIG_IM_FORCE_FABRIC_QUOTA_CHECK 0" "#define CHIP_CONFIG_IM_FORCE_FABRIC_QUOTA_CHECK 1" app_source "${app_source}")
file(WRITE "${app_config}" "${app_source}")
