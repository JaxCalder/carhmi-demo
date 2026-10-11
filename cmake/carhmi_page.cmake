# ======================================================================
#               The page -> the view's sources
# ======================================================================
# assets/car_hmi.html is a page made in HTML, Tailwind CSS and JavaScript.
# todmvs (dmvs_html + dmvs_js) lays it out as a browser does and writes the
# view (carhmi.dmvs) with the fonts and images it draws, next to it; dmod's
# DMOD_ASSETS_PATHS turns them into carhmi.dmv, .dmvf and .dmvi at build time.
#
# The sources have to exist when dmod looks for the assets (configure time),
# so the page is converted when CMake configures; a change of the page or of
# its resources configures the project again.
#
#   carhmi_convert_page(<page> <width>x<height> <element> <name> <output dir>)
#
function(carhmi_convert_page page screen element name outDir)
    find_program(CARHMI_DMF_GET dmf-get HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
    if(NOT DMOD_LOADER)
        find_program(DMOD_LOADER dmod_loader HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
    endif()
    if(NOT CARHMI_DMF_GET OR NOT DMOD_LOADER)
        message(FATAL_ERROR "carhmi: dmf-get and dmod_loader are needed to convert ${page} (build the dmod tools: -DDMOD_BUILD_TOOLS=ON)")
    endif()
    if(NOT DEFINED DMOD_VIEWS_TOOLS_NAME OR "${DMOD_VIEWS_TOOLS_NAME}" STREQUAL "")
        set(DMOD_VIEWS_TOOLS_NAME "arch/x86_64")
    endif()

    # todmvs and the converters it loads by name (dmvsi finds dmvs_html for a
    # page, which compiles its scripts with dmvs_js), for the build host
    set(TOOL_DIR "${CMAKE_BINARY_DIR}/todmvs")
    foreach(TOOL todmvs dmvs_html dmvs_js)
        if(NOT EXISTS "${TOOL_DIR}/${TOOL}.dmf")
            set(GET_OPTIONS -t ${DMOD_VIEWS_TOOLS_NAME} --type dmf -y)
            if(DEFINED DMOD_DMM_URL)
                list(APPEND GET_OPTIONS -m ${DMOD_DMM_URL})
            endif()
            message(STATUS "carhmi: downloading ${TOOL} with dmf-get to ${TOOL_DIR}")
            file(MAKE_DIRECTORY "${TOOL_DIR}")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${TOOL_DIR} ${CARHMI_DMF_GET} ${TOOL} ${GET_OPTIONS} -o ${TOOL_DIR}
                RESULT_VARIABLE GET_RESULT OUTPUT_VARIABLE GET_OUTPUT ERROR_VARIABLE GET_ERROR)
            if(NOT GET_RESULT EQUAL 0 OR NOT EXISTS "${TOOL_DIR}/${TOOL}.dmf")
                message(FATAL_ERROR "carhmi: cannot download ${TOOL}: ${GET_ERROR}${GET_OUTPUT}")
            endif()
        endif()
    endforeach()

    file(MAKE_DIRECTORY "${outDir}")
    get_filename_component(PAGE_DIR "${page}" DIRECTORY)
    file(GLOB_RECURSE PAGE_FILES CONFIGURE_DEPENDS "${PAGE_DIR}/*")
    set(STAMP "${outDir}/.stamp")
    set(STALE FALSE)
    if(NOT EXISTS "${outDir}/${name}.dmvs" OR NOT EXISTS "${STAMP}")
        set(STALE TRUE)
    else()
        foreach(FILE ${PAGE_FILES})
            if("${FILE}" IS_NEWER_THAN "${STAMP}")
                set(STALE TRUE)
                break()
            endif()
        endforeach()
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PAGE_FILES})

    if(STALE)
        message(STATUS "carhmi: converting ${page} (${element}, ${screen}) to ${outDir}/${name}.dmvs")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${TOOL_DIR}
                ${DMOD_LOADER} ${TOOL_DIR}/todmvs.dmf --args
                -q -r ${element} -s ${screen} -n ${name} -o ${outDir}/${name}.dmvs ${page}
            RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
        if(NOT RESULT EQUAL 0 OR NOT EXISTS "${outDir}/${name}.dmvs")
            message(FATAL_ERROR "carhmi: todmvs failed (${RESULT}): ${ERROR}${OUTPUT}")
        endif()
        file(TOUCH "${STAMP}")
    endif()
endfunction()
