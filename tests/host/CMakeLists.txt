find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(solar_host_smoke smoke.cpp)
target_link_libraries(solar_host_smoke PRIVATE solar)

add_test(NAME solar.host.smoke COMMAND solar_host_smoke)

add_executable(solar_host_core core.cpp)
target_link_libraries(solar_host_core PRIVATE solar)

add_test(NAME solar.host.core COMMAND solar_host_core)

add_executable(solar_host_parameter_store parameter_store.cpp)
target_link_libraries(solar_host_parameter_store PRIVATE solar)
add_test(NAME solar.host.parameters.store COMMAND solar_host_parameter_store)

add_executable(solar_host_system_composer system_composer.cpp)
target_link_libraries(solar_host_system_composer PRIVATE solar)
add_test(NAME solar.host.system.composer COMMAND solar_host_system_composer)

add_executable(solar_host_system_dispatch system_dispatch.cpp)
target_link_libraries(solar_host_system_dispatch PRIVATE solar)
add_test(NAME solar.host.system.dispatch COMMAND solar_host_system_dispatch)

add_executable(solar_host_remote_server remote_server.cpp)
target_link_libraries(solar_host_remote_server PRIVATE solar)
add_test(NAME solar.host.remote.server COMMAND solar_host_remote_server)

add_executable(solar_host_module_foundation module_foundation.cpp)
target_link_libraries(solar_host_module_foundation PRIVATE solar)
add_test(NAME solar.host.module.foundation COMMAND solar_host_module_foundation)

add_executable(solar_host_standalone_modules standalone_modules.cpp)
target_compile_definitions(solar_host_standalone_modules PRIVATE
    CONFIG_SOLAR_LOG=1
    CONFIG_SOLAR_LOG_MAX_RECORD_BYTES=128
    CONFIG_SOLAR_LOG_MAX_STRING_BYTES=64
    CONFIG_SOLAR_LOG_MAX_HEXDUMP_BYTES=64
    CONFIG_SOLAR_LOG_RENDER_BUFFER_BYTES=256)
target_link_libraries(solar_host_standalone_modules PRIVATE solar)
add_test(NAME solar.host.modules.standalone COMMAND solar_host_standalone_modules)

add_executable(solar_host_standalone_modules_concurrency standalone_modules_concurrency.cpp)
target_link_libraries(solar_host_standalone_modules_concurrency PRIVATE solar)
add_test(NAME solar.host.modules.standalone_concurrency
    COMMAND solar_host_standalone_modules_concurrency)

set(solar_application_generated_dir
    ${CMAKE_CURRENT_BINARY_DIR}/application-generated)
set(solar_application_generated_header
    ${solar_application_generated_dir}/solar/generated/app.hpp)
add_custom_command(
    OUTPUT ${solar_application_generated_header}
    BYPRODUCTS
        ${solar_application_generated_dir}/solar/generated/remote.hpp
        ${solar_application_generated_dir}/solar/generated/types.hpp
        ${solar_application_generated_dir}/solar/generated/parameters.hpp
        ${solar_application_generated_dir}/solar/generated/contract.hpp
        ${solar_application_generated_dir}/interface.ir.json
        ${solar_application_generated_dir}/manifest.json
        ${solar_application_generated_dir}/compatibility.json
        ${solar_application_generated_dir}/interface.d
        ${solar_application_generated_dir}/solar.interface.lock
    COMMAND ${CMAKE_COMMAND} -E make_directory ${solar_application_generated_dir}
    COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tools/generate_solar.py
        --project ${PROJECT_SOURCE_DIR}/tests/application_fixture/solar.project.yaml
        --output ${solar_application_generated_dir}
        --lock ${solar_application_generated_dir}/solar.interface.lock
        --update-lock
    DEPENDS
        ${PROJECT_SOURCE_DIR}/tools/generate_solar.py
        ${PROJECT_SOURCE_DIR}/tools/solar_codegen/compiler.py
        ${PROJECT_SOURCE_DIR}/tests/application_fixture/solar.project.yaml
        ${PROJECT_SOURCE_DIR}/tests/application_fixture/robot.solar.yaml
    DEPFILE ${solar_application_generated_dir}/interface.d
    VERBATIM)
add_custom_target(solar_application_generate
    DEPENDS ${solar_application_generated_header})

add_executable(solar_host_generated_application
    generated_application.cpp
    ${solar_application_generated_header})
add_dependencies(solar_host_generated_application solar_application_generate)
target_include_directories(solar_host_generated_application
    PRIVATE ${solar_application_generated_dir})
target_link_libraries(solar_host_generated_application PRIVATE solar)
add_test(NAME solar.host.generated.application COMMAND solar_host_generated_application)

add_executable(solar_host_codegen_cmake generated_application.cpp)
target_link_libraries(solar_host_codegen_cmake PRIVATE solar)
solar_generate_contract(
    TARGET solar_host_codegen_cmake
    PROJECT ${PROJECT_SOURCE_DIR}/tests/application_fixture/solar.project.yaml
    LOCK ${CMAKE_CURRENT_BINARY_DIR}/production-codegen/solar.interface.lock
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/production-codegen
    UPDATE_LOCK)
add_test(NAME solar.host.codegen.cmake COMMAND solar_host_codegen_cmake)

add_test(
    NAME solar.host.shipment
    COMMAND ${CMAKE_COMMAND} -E env
        PYTHONPATH=${PROJECT_SOURCE_DIR}/tools
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_shipment.py
        --ir ${CMAKE_CURRENT_BINARY_DIR}/production-codegen/interface.ir.json)
set_tests_properties(solar.host.shipment PROPERTIES
    DEPENDS solar.host.codegen.cmake)

add_executable(solar_host_remote_protocol remote_protocol.cpp)
target_link_libraries(solar_host_remote_protocol PRIVATE solar)
add_test(NAME solar.host.remote_protocol COMMAND solar_host_remote_protocol)

add_executable(solar_host_catalog_link_removal catalog_link_removal.cpp)
target_link_libraries(solar_host_catalog_link_removal PRIVATE solar)

add_test(NAME solar.host.catalog_link_removal COMMAND solar_host_catalog_link_removal)

add_test(
    NAME solar.host.remote_protocol.python
    COMMAND ${CMAKE_COMMAND} -E env
        PYTHONPATH=${PROJECT_SOURCE_DIR}/sdk/python/src
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_remote_host.py
        --vectors ${PROJECT_SOURCE_DIR}/tests/vectors/remote_protocol_v1.json)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The post-link generator consumes ELF images. Mach-O and PE host binaries
    # still exercise protocol declarations, but are not valid generator input.
    add_test(
        NAME solar.host.remote_generation
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/check_remote_generation.py
            --generator ${PROJECT_SOURCE_DIR}/tools/remote/generate_manifest.py
            --elf $<TARGET_FILE:solar_host_remote_protocol>
            --compiler ${CMAKE_CXX_COMPILER})
endif()

add_test(
    NAME solar.host.remote_sdk
    COMMAND ${CMAKE_COMMAND} -E env
        PYTHONPATH=${PROJECT_SOURCE_DIR}/sdk/python/src
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_remote_sdk.py)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pytest, pytest_asyncio"
    RESULT_VARIABLE SOLAR_PYTEST_IMPORT_RESULT
    OUTPUT_QUIET ERROR_QUIET)
if(SOLAR_PYTEST_IMPORT_RESULT EQUAL 0)
    add_test(
        NAME solar.host.remote_sdk.pytest
        COMMAND ${CMAKE_COMMAND} -E env
            PYTHONDONTWRITEBYTECODE=1
            PYTHONPATH=${PROJECT_SOURCE_DIR}/sdk/python/src
            ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
            ${PROJECT_SOURCE_DIR}/sdk/python/tests)
endif()

add_test(
    NAME solar.host.codegen
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen.py
        --generator ${PROJECT_SOURCE_DIR}/tools/generate_solar.py
        --fixture ${PROJECT_SOURCE_DIR}/tests/application_fixture)

add_test(
    NAME solar.host.generated.client
    COMMAND ${CMAKE_COMMAND} -E env
        PYTHONPATH=${PROJECT_SOURCE_DIR}/sdk/python/src
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_generated_client.py
        --output ${solar_application_generated_dir}
        --package robot_fixture_solar)
set_tests_properties(solar.host.generated.client
    PROPERTIES DEPENDS solar.host.generated.application)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import mypy"
    RESULT_VARIABLE SOLAR_MYPY_IMPORT_RESULT
    OUTPUT_QUIET ERROR_QUIET)
if(SOLAR_MYPY_IMPORT_RESULT EQUAL 0)
    add_test(
        NAME solar.host.generated.typing
        COMMAND ${CMAKE_COMMAND} -E env
            MYPYPATH=${solar_application_generated_dir}/python:${PROJECT_SOURCE_DIR}/sdk/python/src
            ${Python3_EXECUTABLE} -m mypy --strict --follow-imports=skip
            ${solar_application_generated_dir}/python/robot_fixture_solar
            ${CMAKE_CURRENT_SOURCE_DIR}/generated_typing.py)
    set_tests_properties(solar.host.generated.typing
        PROPERTIES DEPENDS solar.host.generated.application)
endif()

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

solar_add_compile_failure(
    solar.compile_fail.parameter_store_not_in_schema
    SOLAR_PARAMETERS_NOT_IN_SCHEMA
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/parameter_store_not_in_schema.cpp)
solar_add_compile_failure(
    solar.compile_fail.parameter_store_duplicate_transaction
    SOLAR_PARAMETERS_DUPLICATE_TRANSACTION_ENTRY
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/parameter_store_duplicate_transaction.cpp)
solar_add_compile_failure(
    solar.compile_fail.parameter_store_incompatible_value
    SOLAR_PARAMETERS_INCOMPATIBLE_VALUE
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/parameter_store_incompatible_value.cpp)
solar_add_compile_failure(
    solar.compile_fail.system_composer_duplicate_module
    SOLAR_SYSTEM_DUPLICATE_MODULE_OWNERSHIP
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_composer_duplicate_module.cpp)
solar_add_compile_failure(
    solar.compile_fail.system_composer_missing_dependency
    SOLAR_SYSTEM_MISSING_MODULE_DEPENDENCY
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_composer_missing_dependency.cpp)
solar_add_compile_failure(
    solar.compile_fail.system_composer_cycle
    SOLAR_SYSTEM_MODULE_DEPENDENCY_CYCLE
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_composer_cycle.cpp)
solar_add_compile_failure(
    solar.compile_fail.system_composer_invalid_adapter
    SOLAR_SYSTEM_INVALID_CONNECT_ADAPTER
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_composer_invalid_adapter.cpp)
solar_add_compile_failure(
    solar.compile_fail.system_composer_conflicting_identity
    SOLAR_SYSTEM_CONFLICTING_MODULE_IDENTITY
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_composer_conflicting_identity.cpp)
solar_add_compile_failure(
    solar.compile_fail.system_dispatch_missing_handler
    SOLAR_SYSTEM_ACTION_HANDLER_CARDINALITY
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_dispatch_missing_handler.cpp)
solar_add_compile_failure(
    solar.compile_fail.system_dispatch_duplicate_handler
    SOLAR_SYSTEM_ACTION_HANDLER_CARDINALITY
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_dispatch_duplicate_handler.cpp)
solar_add_compile_failure(
    solar.compile_fail.system_dispatch_invalid_signature
    SOLAR_SYSTEM_INVALID_ACTION_HANDLER
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/system_dispatch_invalid_signature.cpp)

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

add_test(
    NAME solar.compile_fail.result_requires_error_projection
    COMMAND
        ${Python3_EXECUTABLE}
        ${compile_fail_harness}
        --token SOLAR_DIAGNOSTIC_RESULT_REQUIRES_ERROR_PROJECTION
        --
        ${CMAKE_CXX_COMPILER}
        -std=c++23
        -I${PROJECT_SOURCE_DIR}/include
        -c
        ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/result_requires_error_projection.cpp
        -o
        ${CMAKE_CURRENT_BINARY_DIR}/result_requires_error_projection.o)

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
    solar.compile_fail.remote_duplicate_schema_id
    SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_SCHEMA_ID
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_duplicate_schema_id.cpp)

solar_add_compile_failure(
    solar.compile_fail.remote_capability_collision
    SOLAR_DIAGNOSTIC_REMOTE_CAPABILITY_COLLISION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_capability_collision.cpp)

solar_add_compile_failure(
    solar.compile_fail.remote_empty_field_name
    SOLAR_DIAGNOSTIC_REMOTE_EMPTY_FIELD_NAME
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_empty_field_name.cpp)
solar_add_compile_failure(
    solar.compile_fail.remote_duplicate_field_name
    SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_FIELD_NAME
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_duplicate_field_name.cpp)
solar_add_compile_failure(
    solar.compile_fail.remote_field_attribute_collision
    SOLAR_DIAGNOSTIC_REMOTE_FIELD_ATTRIBUTE_COLLISION
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_field_attribute_collision.cpp)
solar_add_compile_failure(
    solar.compile_fail.remote_missing_enum_schema
    SOLAR_DIAGNOSTIC_REMOTE_MISSING_ENUM_SCHEMA
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_missing_enum_schema.cpp)
solar_add_compile_failure(
    solar.compile_fail.remote_unbound_enum_schema
    SOLAR_DIAGNOSTIC_REMOTE_UNBOUND_ENUM_SCHEMA
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_unbound_enum_schema.cpp)
solar_add_compile_failure(
    solar.compile_fail.remote_empty_enum
    SOLAR_DIAGNOSTIC_REMOTE_EMPTY_ENUM
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_empty_enum.cpp)
solar_add_compile_failure(
    solar.compile_fail.remote_duplicate_enum_name
    SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_ENUM_NAME
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_duplicate_enum_name.cpp)
solar_add_compile_failure(
    solar.compile_fail.remote_duplicate_enum_value
    SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_ENUM_VALUE
    ${CMAKE_CURRENT_SOURCE_DIR}/../compile_fail/fixtures/remote_duplicate_enum_value.cpp)

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
