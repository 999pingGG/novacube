#include <novacube/argument_parser.h>
#include <novacube/error_handling.h>

#include <stdbool.h>
#include <string.h>

#define NC__ARGUMENT_VALUE_IS_MISSING(argument) (!*(argument) || (*(argument))[0] == '-')

// Counts the number of arguments in the list that begins at the given position and ends at either the last argument
// or the first argument that starts with "--", whatever is encountered first.
static void nc__argument_parser_count_list_elements(char*** argv, int* total) {
    *total = 0;
    while (**argv) {
        if ((**argv)[0] == '-') {
            break;
        }
        (*argv)++;
        (*total)++;
    }
}

bool nc_argument_parser_parse(char** argv, nc_arguments_t* result) {
    *result = (nc_arguments_t){
        .action = NC_ARGUMENT_ACTION_RUN_GAME,
        .platform = NC_ARGUMENT_PLATFORM_DESKTOP,
    };

    if (!argv[1]) {
        return true;
    }

    for (char** argument = &argv[1]; *argument;) {
        if (strcmp(*argument, "--build-assets") == 0) {
            result->action = NC_ARGUMENT_ACTION_BUILD_ASSETS;

            argument++;
            if (NC__ARGUMENT_VALUE_IS_MISSING(argument)) {
                NC_SET_ERROR("Missing the source assets directory for --build-assets");
                return false;
            }

            result->source_assets_directory = *argument;
            argument++;
            result->assets_to_build = argument;
            nc__argument_parser_count_list_elements(&argument, &result->assets_to_build_count);
        } else if (strcmp(*argument, "--debug") == 0) {
            result->debug = true;
            argument++;
        } else if (strcmp(*argument, "-o") == 0) {
            argument++;
            if (NC__ARGUMENT_VALUE_IS_MISSING(argument)) {
                NC_SET_ERROR("Missing the output file for -o");
                return false;
            }
            result->output_database_file = *argument;
            argument++;
        } else if (strcmp(*argument, "--platform") == 0) {
            argument++;
            if (NC__ARGUMENT_VALUE_IS_MISSING(argument)) {
                NC_SET_ERROR("Missing the platform for --platform, must be \"desktop\" or \"mobile\".");
                return false;
            }

            if (strcmp(*argument, "desktop") == 0) {
                result->platform = NC_ARGUMENT_PLATFORM_DESKTOP;
            } else if (strcmp(*argument, "mobile") == 0) {
                result->platform = NC_ARGUMENT_PLATFORM_MOBILE;
            } else {
                NC_SET_ERROR("The platform for --platform must be one of \"desktop\" or \"mobile\".");
                return false;
            }

            argument++;
        } else if (strcmp(*argument, "--strip-png-metadata") == 0) {
            result->strip_png_metadata = true;
            argument++;
        } else if (strcmp(*argument, "--help") == 0) {
            result->action = NC_ARGUMENT_ACTION_PRINT_HELP;
            return true;
        } else if (strcmp(argv[1], "--version") == 0) {
            result->action = NC_ARGUMENT_ACTION_PRINT_VERSION;
            return true;
        } else {
            NC_SET_ERROR("Unrecognized parameter: %s", *argument);
            return false;
        }
    }

    return true;
}
