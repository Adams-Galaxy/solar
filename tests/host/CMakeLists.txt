add_executable(solar_host_smoke smoke.cpp)
target_link_libraries(solar_host_smoke PRIVATE solar)

add_test(NAME solar.host.smoke COMMAND solar_host_smoke)

add_executable(solar_host_core core.cpp)
target_link_libraries(solar_host_core PRIVATE solar)

add_test(NAME solar.host.core COMMAND solar_host_core)

add_executable(solar_host_remote_protocol remote_protocol.cpp)
target_link_libraries(solar_host_remote_protocol PRIVATE solar)
add_test(NAME solar.host.remote_protocol COMMAND solar_host_remote_protocol)

add_executable(solar_host_catalog catalog.cpp catalog_tu.cpp)
target_link_libraries(solar_host_catalog PRIVATE solar)

add_test(NAME solar.host.catalog COMMAND solar_host_catalog)

add_executable(solar_host_catalog_link_removal catalog_link_removal.cpp)
target_link_libraries(solar_host_catalog_link_removal PRIVATE solar)

add_test(NAME solar.host.catalog_link_removal COMMAND solar_host_catalog_link_removal)

set(system_relaxed_sources
    system.cpp
    system_tu.cpp
    system_inline_no_root.cpp)

add_executable(solar_host_system_relaxed ${system_relaxed_sources})
target_link_libraries(solar_host_system_relaxed PRIVATE solar)
add_test(NAME solar.host.system.relaxed COMMAND solar_host_system_relaxed)

add_executable(solar_host_system_strict system_strict.cpp system_strict_tu.cpp)
target_compile_definitions(solar_host_system_strict
    PRIVATE CONFIG_SOLAR_STRICT_CATALOG_BINDING=1)
target_link_libraries(solar_host_system_strict PRIVATE solar)
add_test(NAME solar.host.system.strict COMMAND solar_host_system_strict)

add_executable(solar_host_system_strict_lazy_standalone system_strict_lazy_standalone.cpp)
target_compile_definitions(solar_host_system_strict_lazy_standalone
    PRIVATE CONFIG_SOLAR_STRICT_CATALOG_BINDING=1)
target_link_libraries(solar_host_system_strict_lazy_standalone PRIVATE solar)
add_test(NAME solar.host.system.strict_lazy_standalone
    COMMAND solar_host_system_strict_lazy_standalone)

include(CheckIPOSupported)
check_ipo_supported(RESULT solar_ipo_supported OUTPUT solar_ipo_error LANGUAGES CXX)

if(solar_ipo_supported)
    add_executable(solar_host_system_lto ${system_relaxed_sources})
    set_property(TARGET solar_host_system_lto PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    target_link_libraries(solar_host_system_lto PRIVATE solar)
    add_test(NAME solar.host.system.lto COMMAND solar_host_system_lto)
else()
    message(WARNING "Solar Stage 03 LTO host test unavailable: ${solar_ipo_error}")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
    NAME solar.host.remote_protocol.python
    COMMAND ${CMAKE_COMMAND} -E env
        PYTHONPATH=${PROJECT_SOURCE_DIR}/tools/remote
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_remote_host.py
        --vectors ${PROJECT_SOURCE_DIR}/tests/vectors/remote_protocol_v1.json)

add_test(
    NAME solar.host.remote_generation
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_remote_generation.py
        --generator ${PROJECT_SOURCE_DIR}/tools/remote/generate_manifest.py
        --elf $<TARGET_FILE:solar_host_remote_protocol>
        --compiler ${CMAKE_CXX_COMPILER})

set(compile_fail_harness
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/expect_failure.py)
set(compile_fail_output
    ${CMAKE_CURRENT_BINARY_DIR}/expected_failure.o)

function(solar_add_compile_failure name token source)
    add_test(
        NAME ${name}
        COMMAND
            ${Python3_EXECUTABLE}
            ${compile_fail_harness}
            --token ${token}
            --
            ${CMAKE_CXX_COMPILER}
            -std=c++23
            -I${PROJECT_SOURCE_DIR}/include
            -I${CMAKE_CURRENT_SOURCE_DIR}
            -c
            ${source}
            -o
            ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
endfunction()

add_test(
    NAME solar.compile_fail.self_test
    COMMAND
        ${Python3_EXECUTABLE}
        ${compile_fail_harness}
        --token SOLAR_DIAGNOSTIC_EXPECTED_FAILURE
        --
        ${CMAKE_CXX_COMPILER}
        -std=c++23
        -c
        ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/expected_failure.cpp
        -o
        ${compile_fail_output})

add_test(
    NAME solar.compile_fail.rejects_success
    COMMAND
        ${Python3_EXECUTABLE}
        ${compile_fail_harness}
        --token SOLAR_DIAGNOSTIC_EXPECTED_FAILURE
        --
        ${CMAKE_CXX_COMPILER}
        -std=c++23
        -c
        ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/unexpected_success.cpp
        -o
        ${CMAKE_CURRENT_BINARY_DIR}/unexpected_success.o)
set_tests_properties(solar.compile_fail.rejects_success PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME solar.compile_fail.requires_cpp23
    COMMAND
        ${Python3_EXECUTABLE}
        ${compile_fail_harness}
        --token SOLAR_DIAGNOSTIC_REQUIRES_CPP23
        --
        ${CMAKE_CXX_COMPILER}
        -std=c++20
        -I${PROJECT_SOURCE_DIR}/include
        -c
        ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/requires_cpp23.cpp
        -o
        ${CMAKE_CURRENT_BINARY_DIR}/requires_cpp23.o)

add_test(
    NAME solar.compile_fail.result_rejects_implicit_status
    COMMAND
        ${Python3_EXECUTABLE}
        ${compile_fail_harness}
        --token SOLAR_DIAGNOSTIC_RESULT_REJECTS_IMPLICIT_STATUS
        --
        ${CMAKE_CXX_COMPILER}
        -std=c++23
        -I${PROJECT_SOURCE_DIR}/include
        -c
        ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/result_rejects_implicit_status.cpp
        -o
        ${CMAKE_CURRENT_BINARY_DIR}/result_rejects_implicit_status.o)

solar_add_compile_failure(
    solar.compile_fail.catalog_missing_descriptor
    SOLAR_DIAGNOSTIC_MISSING_DESCRIPTOR
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_missing_descriptor.cpp)

solar_add_compile_failure(
    solar.compile_fail.remote_duplicate_field
    SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_FIELD_ID
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_duplicate_field.cpp)

solar_add_compile_failure(
    solar.compile_fail.remote_unordered_field
    SOLAR_DIAGNOSTIC_REMOTE_FIELD_ORDER
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_unordered_field.cpp)

solar_add_compile_failure(
    solar.compile_fail.remote_missing_schema
    SOLAR_DIAGNOSTIC_REMOTE_MISSING_SCHEMA
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_missing_schema.cpp)

solar_add_compile_failure(
    solar.compile_fail.remote_packed_unbounded
    SOLAR_DIAGNOSTIC_REMOTE_PACKED_FIELD
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_packed_unbounded.cpp)

solar_add_compile_failure(
    solar.compile_fail.remote_disabled_declaration
    SOLAR_DIAGNOSTIC_REMOTE_DISABLED
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_disabled_declaration.cpp)

solar_add_compile_failure(
    solar.compile_fail.remote_duplicate_schema_id
    SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_SCHEMA_ID
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_duplicate_schema_id.cpp)

solar_add_compile_failure(
    solar.compile_fail.remote_capability_collision
    SOLAR_DIAGNOSTIC_REMOTE_CAPABILITY_COLLISION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_capability_collision.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_empty_name
    SOLAR_DIAGNOSTIC_EMPTY_DESCRIPTOR_NAME
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_empty_name.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_wrong_descriptor
    SOLAR_DIAGNOSTIC_WRONG_DESCRIPTOR_KIND
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_wrong_descriptor.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_duplicate_type
    SOLAR_DIAGNOSTIC_DUPLICATE_DECLARATION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_duplicate_type.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_duplicate_name
    SOLAR_DIAGNOSTIC_DUPLICATE_DESCRIPTOR_NAME
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_duplicate_name.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_duplicate_stable_id
    SOLAR_DIAGNOSTIC_DUPLICATE_STABLE_ID
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_duplicate_stable_id.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_malformed_alias
    SOLAR_DIAGNOSTIC_MALFORMED_RESERVED_ALIAS
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_malformed_alias.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_wrong_contribution_tag
    SOLAR_DIAGNOSTIC_WRONG_CONTRIBUTION_TAG
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_wrong_contribution_tag.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_unregistered_extension_tag
    SOLAR_DIAGNOSTIC_UNREGISTERED_EXTENSION_TAG
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_unregistered_extension_tag.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_duplicate_sources
    SOLAR_DIAGNOSTIC_DUPLICATE_DECLARATION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_duplicate_sources.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_direct_and_contributed_duplicate
    SOLAR_DIAGNOSTIC_DUPLICATE_DECLARATION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_direct_and_contributed_duplicate.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_conflicting_owners
    SOLAR_DIAGNOSTIC_DUPLICATE_DECLARATION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_conflicting_owners.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_missing_stable_id
    SOLAR_DIAGNOSTIC_MISSING_STABLE_ID
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_missing_stable_id.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_unregistered_lookup
    SOLAR_DIAGNOSTIC_UNREGISTERED_CATALOG_DECLARATION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_unregistered_lookup.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_dependency_cycle
    SOLAR_DIAGNOSTIC_CATALOG_DEPENDENCY_CYCLE
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_dependency_cycle.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_missing_dependency
    SOLAR_DIAGNOSTIC_UNREGISTERED_CATALOG_DEPENDENCY
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_missing_dependency.cpp)

solar_add_compile_failure(
    solar.compile_fail.catalog_shared_domain_duplicate
    SOLAR_DIAGNOSTIC_DUPLICATE_STABLE_ID
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/catalog_shared_domain_duplicate.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_unknown_section
    SOLAR_DIAGNOSTIC_UNKNOWN_BLUEPRINT_SECTION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_unknown_section.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_duplicate_section
    SOLAR_DIAGNOSTIC_DUPLICATE_BLUEPRINT_SECTION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_duplicate_section.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_duplicate_component
    SOLAR_DIAGNOSTIC_DUPLICATE_COMPONENT
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_duplicate_component.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_malformed_dependencies
    SOLAR_DIAGNOSTIC_MALFORMED_COMPONENT_DEPENDENCIES
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_malformed_dependencies.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_missing_dependency
    SOLAR_DIAGNOSTIC_MISSING_COMPONENT_DEPENDENCY
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_missing_dependency.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_dependency_cycle
    SOLAR_DIAGNOSTIC_COMPONENT_DEPENDENCY_CYCLE
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_dependency_cycle.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_disabled_builtin
    SOLAR_DIAGNOSTIC_DISABLED_REQUIRED_BUILTIN
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_disabled_builtin.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_missing_builtin_requirement
    SOLAR_DIAGNOSTIC_MISSING_BUILTIN_REQUIREMENT
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_missing_builtin_requirement.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_invalid_policy
    SOLAR_DIAGNOSTIC_INVALID_SUBSYSTEM_POLICY
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_invalid_policy.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_duplicate_policy_axis
    SOLAR_DIAGNOSTIC_DUPLICATE_SUBSYSTEM_POLICY_AXIS
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_duplicate_policy_axis.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_unavailable_policy
    SOLAR_DIAGNOSTIC_UNAVAILABLE_SUBSYSTEM_POLICY
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_unavailable_policy.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_incompatible_configuration
    SOLAR_DIAGNOSTIC_INCOMPATIBLE_SUBSYSTEM_CONFIGURATION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_incompatible_configuration.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_missing_binding
    SOLAR_DIAGNOSTIC_MISSING_SYSTEM_BINDING
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_missing_binding.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_duplicate_binding
    SOLAR_DIAGNOSTIC_DUPLICATE_SYSTEM_BINDING
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_duplicate_binding.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_invalid_binding
    SOLAR_DIAGNOSTIC_INVALID_BOUND_SYSTEM
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_invalid_binding.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_strict_unregistered
    SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_OPERATION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_strict_unregistered.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_strict_lazy_without_binding
    SOLAR_DIAGNOSTIC_MISSING_SYSTEM_BINDING
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_strict_lazy_without_binding.cpp)

solar_add_compile_failure(
    solar.compile_fail.system_strict_eager_without_binding
    SOLAR_DIAGNOSTIC_MISSING_SYSTEM_BINDING
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_strict_eager_without_binding.cpp)
