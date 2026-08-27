#include <stdbool.h>
#include <stdio.h>

#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <novacube/argument_parser.h>
#include <novacube/asset_baker.h>

int main(const int argc, char** argv) {
    (void)argc;
    SDL_SetMainReady();
    if (!SDL_Init(0)) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    nc_arguments_t arguments;
    if (!nc_argument_parser_parse(argv, &arguments)) {
        fprintf(stderr, "%s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (arguments.action != NC_ARGUMENT_ACTION_BUILD_ASSETS) {
        fprintf(stderr, "The standalone asset baker requires --build-assets.\n");
        SDL_Quit();
        return 1;
    }

    const nc_asset_baker_options_t options = {
        .source_assets_directory = arguments.source_assets_directory,
        .output_database_file = arguments.output_database_file,
        .texconv_executable = arguments.texconv_executable,
        .astcenc_executable = arguments.astcenc_executable,
        .assets_to_build = arguments.assets_to_build,
        .assets_to_build_count = arguments.assets_to_build_count,
        .platform = arguments.platform == NC_ARGUMENT_PLATFORM_MOBILE
                ? NC_ASSET_BAKER_PLATFORM_MOBILE
                : NC_ASSET_BAKER_PLATFORM_DESKTOP,
        .debug = arguments.debug,
        .strip_png_metadata = arguments.strip_png_metadata,
    };
    if (!nc_asset_baker_bake_assets(&options)) {
        fprintf(stderr, "%s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Quit();
    return 0;
}
