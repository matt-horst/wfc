#pragma once
#include <raylib.h>

void wfc_init(int seed, int rows, int cols, char *tile_set, int depth);
void wfc_step();
void wfc_draw(Texture texture);
int wfc_tile_width();
int wfc_tile_height();
