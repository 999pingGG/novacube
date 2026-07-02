execute_process(
        COMMAND git describe --tags --dirty --always
        WORKING_DIRECTORY "${NC_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${NC_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

configure_file(
        "${NC_BUILD_INFO_TEMPLATE}"
        "${NC_BUILD_INFO_HEADER}"
        @ONLY)
