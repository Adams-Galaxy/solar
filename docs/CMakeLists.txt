cmake_minimum_required(VERSION 3.20)

project(solar_documentation VERSION 0.1.0 LANGUAGES NONE)

find_program(SPHINX_BUILD sphinx-build REQUIRED)
find_program(DOXYGEN_EXECUTABLE doxygen REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SOLAR_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(SOLAR_DOXYGEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/doxygen)
set(SOLAR_DOXYGEN_XML ${SOLAR_DOXYGEN_DIR}/xml)
set(SOLAR_HTML_DIR ${CMAKE_CURRENT_BINARY_DIR}/html)
set(SOLAR_LINKCHECK_DIR ${CMAKE_CURRENT_BINARY_DIR}/linkcheck)
set(SOLAR_SITE_DIR ${CMAKE_CURRENT_BINARY_DIR}/site)
set(SOLAR_KCONFIG_REFERENCE
    ${CMAKE_CURRENT_LIST_DIR}/reference/generated/kconfig.md)

file(GLOB_RECURSE SOLAR_PUBLIC_HEADERS CONFIGURE_DEPENDS
    ${SOLAR_ROOT}/include/solar/*.hpp)

configure_file(Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

add_custom_command(
    OUTPUT ${SOLAR_DOXYGEN_XML}/index.xml
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SOLAR_DOXYGEN_DIR}
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        ${SOLAR_PUBLIC_HEADERS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Generating Solar Doxygen XML"
    VERBATIM)

add_custom_target(docs-doxygen DEPENDS ${SOLAR_DOXYGEN_XML}/index.xml)

add_custom_command(
    OUTPUT ${SOLAR_KCONFIG_REFERENCE}
    COMMAND ${Python3_EXECUTABLE}
        ${SOLAR_ROOT}/tools/docs/generate_kconfig_reference.py
        --kconfig ${SOLAR_ROOT}/zephyr/Kconfig
        --output ${SOLAR_KCONFIG_REFERENCE}
    DEPENDS
        ${SOLAR_ROOT}/zephyr/Kconfig
        ${SOLAR_ROOT}/tools/docs/generate_kconfig_reference.py
    COMMENT "Generating Solar Kconfig reference"
    VERBATIM)

add_custom_target(docs-reference DEPENDS ${SOLAR_KCONFIG_REFERENCE})

add_custom_target(
    docs-html
    COMMAND
        ${CMAKE_COMMAND} -E env
        SOLAR_DOXYGEN_XML=${SOLAR_DOXYGEN_XML}
        ${SPHINX_BUILD} -W --keep-going -n -b html
        ${CMAKE_CURRENT_LIST_DIR} ${SOLAR_HTML_DIR}
    DEPENDS docs-doxygen docs-reference
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Building Solar HTML documentation"
    VERBATIM)

add_custom_target(
    docs-linkcheck
    COMMAND
        ${CMAKE_COMMAND} -E env
        SOLAR_DOXYGEN_XML=${SOLAR_DOXYGEN_XML}
        ${SPHINX_BUILD} -W --keep-going -n -b linkcheck
        ${CMAKE_CURRENT_LIST_DIR} ${SOLAR_LINKCHECK_DIR}
    DEPENDS docs-doxygen docs-reference
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Checking Solar documentation links"
    VERBATIM)

add_custom_target(
    docs-site
    COMMAND ${Python3_EXECUTABLE}
        ${SOLAR_ROOT}/tools/docs/package_site.py
        --html ${SOLAR_HTML_DIR}
        --site ${SOLAR_SITE_DIR}
        --version ${PROJECT_VERSION}
    DEPENDS docs-html ${SOLAR_ROOT}/tools/docs/package_site.py
    COMMENT "Packaging versioned Solar documentation site"
    VERBATIM)

add_custom_target(
    docs-audit
    COMMAND ${Python3_EXECUTABLE}
        ${SOLAR_ROOT}/tools/docs/audit_documentation.py
        --root ${SOLAR_ROOT}
    DEPENDS docs-site docs-linkcheck
        ${SOLAR_ROOT}/tools/docs/audit_documentation.py
    COMMENT "Auditing Solar documentation coverage"
    VERBATIM)
