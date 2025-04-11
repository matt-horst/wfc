#include <raylib.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>

#include "wfc.h"

static int seed = -1;
static int width = 800;
static int height = 800;
static int rows = 10;
static int cols = 10;
static int depth = 4;
static char *tile_set = "";
static char *src_image = "";

void print_usage() {
    printf("Usage: main [options]\n");
    printf("  -s <seed_value>\n");
    printf("  -w <window width>\n");
    printf("  -h <window height>\n");
    printf("  -r <tile rows>\n");
    printf("  -c <tile columns>\n");
    printf("  -d <max recursive depth>\n");
    printf("  -t <tile set>\n");
    printf("  -i <source image>\n");
}

void parse_args(int argc, char **argv) {
    opterr = 0;

    int c;
    while ((c = getopt(argc, argv, "s:w:h:r:c:d:t:i:")) != -1) {
        switch (c) {
            case 's':
                seed = atoi(optarg);
                break;
            case 'w':
                width = atoi(optarg);
                break;
            case 'h':
                height = atoi(optarg);
                break;
            case 'r':
                rows = atoi(optarg);
                break;
            case 'c':
                cols = atoi(optarg);
                break;
            case 'd':
                depth = atoi(optarg);
                break;
            case 't':
                tile_set = optarg;
                break;
            case 'i':
                src_image = optarg;
                break;

            case '?':
                exit(EXIT_FAILURE);
            default:
                exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char **argv) {
    parse_args(argc, argv);

    wfc_init(seed, rows, cols, tile_set, depth);

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);

    InitWindow(width, height, "WFC");

    SetTargetFPS(30);

    Image blank = GenImageColor(rows * wfc_tile_width(), cols * wfc_tile_height(), BLACK);
    ImageFormat(&blank, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    Texture texture = LoadTextureFromImage(blank);
    UnloadImage(blank);

    while (!WindowShouldClose()) {
        wfc_step();
        wfc_draw(texture);
    }

    CloseWindow();

    return 0;
}
