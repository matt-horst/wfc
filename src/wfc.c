#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <raylib.h>

#include "wfc.h"

#define MAX_TILES 128
#define MAX_DEPTH 4

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

enum Direction {
    UP, RIGHT, DOWN, LEFT
};

enum Matches {
    UP_MATCH = 1,
    RIGHT_MATCH = 2,
    DOWN_MATCH = 4,
    LEFT_MATCH = 8
};

typedef struct Tile {
    Image img;
    struct Tile *options[4][MAX_TILES];
    int num_options[4];
    float frequency;
    int hash_value;
    int id;
} Tile;

Tile *tiles[MAX_TILES];
int tile_idx = 0;
int num_tiles = 0;

uint64_t tile_hash(Tile *tile) {
    uint64_t hash = 5381;
    size_t bytes = tile->img.width * tile->img.height;
    uint8_t *data = (uint8_t *) tile->img.data;

    for (int i = 0; i < bytes; i++) {
        int c = *((uint8_t *) data++);
        hash = (hash << 5) + hash + c;
    }

    return hash;
}

static int tile_id = 0;

Tile *tile_create_from_image(char *file, float weight) {
    Tile *tile = calloc(1, sizeof(Tile));

    tile->img = LoadImage(file);
    ImageFormat(&tile->img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    tile->hash_value = tile_hash(tile);
    tile->frequency = weight;
    tile->id = tile_id++;

    return tile;
}

Tile *tile_rotate90(Tile *src) {
    Tile *dest = calloc(1, sizeof(Tile));

    dest->frequency = src->frequency;
    dest->id = tile_id++;

    dest->img = ImageCopy(src->img);
    ImageRotateCW(&dest->img);

    return dest;
}

Tile *tile_rotate180(Tile *src) {
    Tile *dest = calloc(1, sizeof(Tile));

    dest->frequency = src->frequency;
    dest->id = tile_id++;

    dest->img = ImageCopy(src->img);
    ImageRotateCW(&dest->img);
    ImageRotateCW(&dest->img);

    return dest;
}

Tile *tile_rotate270(Tile *src) {
    Tile *dest = calloc(1, sizeof(Tile));

    dest->frequency = src->frequency;
    dest->id = tile_id++;

    dest->img = ImageCopy(src->img);
    ImageRotateCCW(&dest->img);

    return dest;
}

Tile *tile_mirror_horz(Tile *src) {
    Tile *dest = calloc(1, sizeof(Tile));

    dest->frequency = src->frequency;
    dest->id = tile_id++;

    dest->img = ImageCopy(src->img);
    ImageFlipHorizontal(&dest->img);

    return dest;
}

Tile *tile_mirror_vert(Tile *src) {
    Tile *dest = calloc(1, sizeof(Tile));

    dest->frequency = src->frequency;
    dest->id = tile_id++;

    dest->img = ImageCopy(src->img);
    ImageFlipVertical(&dest->img);

    return dest;
}

int tile_matches(Tile *a, Tile *b) {
    int result = ~0;

    int w = a->img.width;
    int h = a->img.height;
    int pitch = w * 3;

    void *pixel_a_top = a->img.data;
    void *pixel_a_bot = a->img.data + (pitch * (h - 1));

    void *pixel_b_top = b->img.data;
    void *pixel_b_bot = b->img.data + (pitch * (h - 1));

    if (memcmp(pixel_a_top, pixel_b_bot, w * 3)) {
        result &= ~UP_MATCH;
    }

    if (memcmp(pixel_a_bot, pixel_b_top, w * 3)) {
        result &= ~DOWN_MATCH;
    }

    for (int i = 0; i < h; i++) {
        uint8_t *pixel_a_left = (uint8_t *) a->img.data + (i * pitch);
        uint8_t *pixel_a_right = (uint8_t *) a->img.data + (i * pitch + (w - 1) * 3);
        uint8_t *pixel_b_left = (uint8_t *) b->img.data + (i * pitch);
        uint8_t *pixel_b_right = (uint8_t *) b->img.data + (i * pitch + (w - 1) * 3);

        if (memcmp(pixel_a_right, pixel_b_left, 3)) {
            result &= ~RIGHT_MATCH;
        }

        if (memcmp(pixel_a_left, pixel_b_right, 3)) {
            result &= ~LEFT_MATCH;
        }

        if (!(result & RIGHT_MATCH || result & LEFT_MATCH)) {
            break;
        }
    }

    return result;
}

typedef struct Cell {
    int row;
    int col;
    bool collapsed;
    Tile **options;
    int num_options;
    float entropy;
    int heap_idx;
    bool new;
} Cell;

typedef struct{
    Cell *cells;
    int rows;
    int cols;
} Grid;

typedef struct  {
    int r;
    int c;
    float entropy;
} HeapNode;

static HeapNode *min_heap = NULL;
static int heap_size = 0;

static Grid *grid = NULL;

static int depth = 0;

void sift_up(int c) {
    int p = (c - 1) / 2;

    while (p >= 0 && min_heap[p].entropy > min_heap[c].entropy) {
        HeapNode temp = min_heap[p];
        min_heap[p] = min_heap[c];
        min_heap[c] = temp;

        grid->cells[min_heap[p].r * grid->cols + min_heap[p].c].heap_idx = p;
        grid->cells[min_heap[c].r * grid->cols + min_heap[c].c].heap_idx = c;

        c = p;
        p = (c - 1) / 2;
    }
}

void sift_down(int p) {
    while (p <= (heap_size - 2) / 2) {
        int c = 2 * p + 1;
        if (min_heap[c].entropy < min_heap[c + 1].entropy) {
            if (min_heap[c].entropy < min_heap[p].entropy) {
                HeapNode temp = min_heap[p];
                min_heap[p] = min_heap[c];
                min_heap[c] = temp;

                grid->cells[min_heap[p].r * grid->cols + min_heap[p].c].heap_idx = p;
                grid->cells[min_heap[c].r * grid->cols + min_heap[c].c].heap_idx = c;

                p = c;
            } else {
                break;
            }
        } else {
            if (min_heap[c + 1].entropy < min_heap[p].entropy) {
                HeapNode temp = min_heap[p];
                min_heap[p] = min_heap[c + 1];
                min_heap[c + 1] = temp;

                grid->cells[min_heap[p].r * grid->cols + min_heap[p].c].heap_idx = p;
                grid->cells[min_heap[c + 1].r * grid->cols + min_heap[c + 1].c].heap_idx = c + 1;

                p = c + 1;
            } else {
                break;
            }
        }
   }
}

int find(int r, int c) {
    for (int i = 0; i < heap_size; i++) {
        if (min_heap[i].r == r && min_heap[i].c == c) {
            return i;
        }
    }

    return -1;
}

void heap_insert(HeapNode node) {
    min_heap[heap_size++] = node;
    grid->cells[node.r * grid->cols + node.c].heap_idx = heap_size - 1;
    sift_up(heap_size - 1);
}

HeapNode heap_extract() {
    HeapNode root = min_heap[0];
    grid->cells[root.r * grid->cols + root.c].heap_idx = -1;

    min_heap[0] = min_heap[--heap_size];
    grid->cells[min_heap[0].r * grid->cols + min_heap[0].c].heap_idx = 0;
    sift_down(0);

    return root;
}

HeapNode heap_remove(int idx) {
    assert(idx >= 0);
    HeapNode node = min_heap[idx];
    grid->cells[node.r * grid->cols + node.c].heap_idx = -1;

    min_heap[idx] = min_heap[--heap_size];
    grid->cells[min_heap[idx].r * grid->cols + min_heap[idx].c].heap_idx = idx;
    sift_down(idx);

    return node;
}

void heap_reset() {
    heap_size = 0;
    for (int i = 0; i < grid->rows; i++) {
        for (int j = 0; j < grid->cols; j++) {
            Cell *cell = &grid->cells[i * grid->cols + j];
            if (!cell->collapsed) {
                heap_insert((HeapNode) {.r = i, .c = j, .entropy = cell->entropy});
            }
        }
    }
}


void cell_collapse(Cell *cell) {
    float *cumulative_weight = calloc(cell->num_options + 1, sizeof(float));
    
    for (int i = 0; i < cell->num_options; i++) {
        cumulative_weight[i + 1] = cumulative_weight[i] + cell->options[i]->frequency;
    }

    float v = ((float) rand()) / RAND_MAX * cumulative_weight[cell->num_options];

    int l = 0;
    int r = cell->num_options;

    while (l <= r) {
        int m = l + (r - l) / 2;
        if (cumulative_weight[m] < v) {
            l = m + 1;
        } else {
            r = m - 1;
        }
    }

    assert(0 <= r && r < cell->num_options);

    cell->collapsed = true;
    cell->new = true;
    Tile *chosen = cell->options[r];
    Tile *tmp = cell->options[0];
    cell->options[0] = chosen;
    cell->options[r] = tmp;
    cell->num_options = 1;

    free(cumulative_weight);
}

float cell_calc_entropy(Cell *cell) {
    float total_weight = 0.0;
    float max_weight = 0.0;

    if (cell->num_options == 0) {
        return 0.0;
    }

    for (int i = 0; i < cell->num_options; i++) {
        total_weight += cell->options[i]->frequency;
        max_weight = MAX(max_weight, cell->options[i]->frequency);
    }

    return total_weight / max_weight;
}

void cell_reset(Cell *cell) {
    memcpy(cell->options, tiles, num_tiles * sizeof(Tile *));
    cell->num_options = num_tiles;
    cell->new = true;
    cell->entropy = INFINITY;
    cell->collapsed = false;
}

typedef struct {
    int r;
    int c;
    int options;
} StackNode;

static StackNode *stack;
static int stack_size = 0;

Grid *grid_create(int rows, int cols) {
    Grid *grid = malloc(sizeof(Grid));
    *grid = (Grid) {.rows = rows, .cols = cols, .cells = malloc(rows * cols * sizeof(Cell))};

    for (int i = 0; i < rows * cols; i++) {
        grid->cells[i] = (Cell) {
            .row = i / cols,
            .col = i % cols,
            .collapsed = false, 
            .new = false,
            .num_options = num_tiles, 
            .options = malloc(num_tiles * sizeof(void *)), 
            .entropy = INFINITY
        };

        memcpy(grid->cells[i].options, tiles, num_tiles * sizeof(void *));
    }

    return grid;
}

void grid_free(Grid *grid) {
    free(grid->cells);
    free(grid);
}

void grid_reset(Grid *grid) {
    for (int i = 0; i < grid->rows * grid->cols; i++) {
        Cell *cell = &grid->cells[i];
        if (!cell->collapsed && cell->num_options != num_tiles) {
            cell_reset(cell);
        }
    }
}

bool propogate_options(Grid *grid, int r, int c, int depth) {
    typedef struct {
        int r;
        int c;
        int depth;
    } QueueNode;

    bool conflict = false;

    bool *visited = calloc(grid->rows * grid->cols, sizeof(bool));
    int queue_size = grid->rows * grid->cols * 2;
    QueueNode *queue = calloc(queue_size, sizeof(QueueNode));
    int queue_start = 0;
    int queue_end = 0;

    queue[queue_end++] = (QueueNode) {.r = r, .c = c, .depth = 0};
    visited[r * grid->cols + c] = true;

    while (queue_end > queue_start) {
        const QueueNode *node = &queue[queue_start++ % queue_size];

        if (node->depth > depth) {
            break;
        }


        Cell *cell = &grid->cells[node->r * grid->cols + node->c];
        Cell *adj[4] = {NULL, NULL, NULL, NULL};

        /* Add adjacent cells to the queue */
        if (node->r > 0) {
            queue[queue_end++ % queue_size] = (QueueNode) {.r = node->r - 1, .c = node->c, .depth = node->depth + 1};
            adj[UP] = &grid->cells[(node->r - 1) * grid->cols + node->c];
        }

        if (node->r < grid->rows - 1) {
            queue[queue_end++ % queue_size] = (QueueNode) {.r = node->r + 1, .c = node->c, .depth = node->depth + 1};
            adj[DOWN] = &grid->cells[(node->r + 1) * grid->cols + node->c];
        }

        if (node->c > 0) {
            queue[queue_end++ % queue_size] = (QueueNode) {.r = node->r, .c = node->c - 1, .depth = node->depth + 1};
            adj[LEFT] = &grid->cells[node->r * grid->cols + node->c - 1];
        }

        if (node->c < grid->cols - 1) {
            queue[queue_end++ % queue_size] = (QueueNode) {.r = node->r, .c = node->c + 1, .depth = node->depth + 1};
            adj[RIGHT] = &grid->cells[node->r * grid->cols + node->c + 1];
        }

        if (visited[node->r * grid->cols + node->c]) {
            continue;
        } 

        visited[node->r * grid->cols + node->c] = true;

        /* Take all options from neighbors */
        if (!cell->collapsed) {
            bool set_options[4][MAX_TILES] = {0, };

            for (int i = 0; i < 4; i++) {
                Cell *adj_cell = adj[i];
                if (adj_cell != NULL) {
                    for (int j = 0; j < adj_cell->num_options; j++) {
                        Tile *opt = adj_cell->options[j];
                        for (int k = 0; k < opt->num_options[(i + 2) % 4]; k++) {
                            Tile *opt_adj = opt->options[(i + 2) % 4][k];
                            set_options[i][opt_adj->id] = true;
                        }
                    }
                } else {
                    for (int j = 0; j < num_tiles; j++) {
                        set_options[i][j] = true;
                    }
                }
            }

            int num_new_options = 0;
            for (int i = 0; i < num_tiles; i++) {
                bool all = true;
                for (int j = 0; j < 4; j++) {
                    all = all && set_options[j][i];
                }

                if (all) {
                    cell->options[num_new_options++] = tiles[i];
                }
            }

            if (num_new_options != cell->num_options) {
                cell->num_options = num_new_options;
                cell->new = true;
                int idx = find(node->r, node->c);
                // int idx2 = cell->heap_idx;
                // assert(idx == idx2);
                if (cell->num_options == 0) {
                    conflict = true;
                    break;
                } else if (idx >= 0) {
                    HeapNode heap_node = heap_remove(idx);
                    heap_node.entropy = (cell->entropy = cell_calc_entropy(cell));
                    heap_insert(heap_node);
                }
            }
        }
    }

    free(queue);
    free(visited);

    return !conflict;
}

void wfc_step();

void wfc_init(int seed, int rows, int cols, char *tile_set, int max_depth) {
    if (seed < 0) {
        srand(time(NULL));
    } else {
        srand(seed);
    }

    depth = max_depth;

    grid = grid_create(rows, cols);

    char *dir_name = tile_set;
    char schema[128];
    int err = snprintf(schema, 127, "%s/schema", dir_name);
    if (err < 0) {
        exit(EXIT_FAILURE);
    }
    FILE *schema_file = fopen(schema, "r");
    char *line = NULL;
    size_t bytes = 0;

    while (getline(&line, &bytes, schema_file) != EOF) {
        char tile_name[128];
        float weight;
        int r90, r180, r270, mh, mv;

        sscanf(line, "%s %f %d %d %d %d %d", tile_name, &weight, &r90, &r180, &r270, &mh, &mv);

        char file_name[128];
        int err = snprintf(file_name, 127, "%s/%s.png", dir_name, tile_name);
        if (err < 0) {
            exit(EXIT_FAILURE);
        }

        Tile *base_tile = tile_create_from_image(file_name, weight);
        Tile *mh_tile = NULL;
        Tile *mv_tile = NULL;

        tiles[num_tiles++] = base_tile;

        if (mh) {
            mh_tile = tile_mirror_horz(base_tile);
            tiles[num_tiles++] = mh_tile;
        }

        if (mv) {
            mv_tile = tile_mirror_horz(base_tile);
            tiles[num_tiles++] = mv_tile;
        }

        if (r90) {
            tiles[num_tiles++] = tile_rotate90(base_tile);

            if (mh) {
                tiles[num_tiles++] = tile_rotate90(mh_tile);
            }

            if (mv) {
                tiles[num_tiles++] = tile_rotate90(mv_tile);
            }
        }

        if (r180) {
            tiles[num_tiles++] = tile_rotate180(base_tile);

            if (mh) {
                tiles[num_tiles++] = tile_rotate180(mh_tile);
            }

            if (mv) {
                tiles[num_tiles++] = tile_rotate180(mv_tile);
            }
        }

        if (r270) {
            tiles[num_tiles++] = tile_rotate270(base_tile);

            if (mh) {
                tiles[num_tiles++] = tile_rotate270(mh_tile);
            }

            if (mv) {
                tiles[num_tiles++] = tile_rotate270(mv_tile);
            }
        }
    }
    free(line);
    fclose(schema_file);

    /* Define the adjacency rules for each tile */
    for (int i = 0; i < num_tiles - 1; i++) {
        for (int j = i;  j < num_tiles; j++) {
            Tile *tile_a = tiles[i], *tile_b = tiles[j];

            int matches = tile_matches(tile_a, tile_b);

            if (matches & UP_MATCH) {
                tile_a->options[UP][tile_a->num_options[UP]++] = tile_b;
                if (i != j) {
                    tile_b->options[DOWN][tile_b->num_options[DOWN]++] = tile_a;
                }
            }

            if (matches & RIGHT_MATCH) {
                tile_a->options[RIGHT][tile_a->num_options[RIGHT]++] = tile_b;
                if (i != j) {
                    tile_b->options[LEFT][tile_b->num_options[LEFT]++] = tile_a;
                }
            }

            if (matches & DOWN_MATCH) {
                tile_a->options[DOWN][tile_a->num_options[DOWN]++] = tile_b;
                if (i != j) {
                    tile_b->options[UP][tile_b->num_options[UP]++] = tile_a;
                }
            }

            if (matches & LEFT_MATCH) {
                tile_a->options[LEFT][tile_a->num_options[LEFT]++] = tile_b;
                if (i != j) {
                    tile_b->options[RIGHT][tile_b->num_options[RIGHT]++] = tile_a;
                }
            }
        }
    }

    grid = grid_create(rows, cols);

    /* Pick a random cell and collapse it */
    int idx = rand() % (grid->rows * grid->cols);
    
    cell_collapse(&grid->cells[idx]);

    /* Intialize the min heap */
    min_heap = malloc(grid->rows * grid->cols * sizeof(HeapNode));

    heap_reset();

    /* Initialize the recursive stack */
    stack = malloc(grid->rows * grid->cols * sizeof(StackNode));

    propogate_options(grid, idx / grid->cols, idx % grid->cols, depth);

}

void wfc_step() {
    static bool conflict = false;
    if (heap_size > 0 && !conflict) {
        HeapNode root = heap_extract();
        Cell *cell = &grid->cells[root.r * grid->cols + root.c];
        stack[stack_size++] = (StackNode) {.r = root.r, .c = root.c, .options = cell->num_options};
        cell_collapse(cell);

        if (!propogate_options(grid, root.r, root.c, depth)) {
            conflict = true;
            printf("CONFLICT\n");
        }
    } else if (conflict) {
        grid_reset(grid);

        StackNode top = stack[--stack_size];
        Cell *prev = &grid->cells[top.r * grid->cols + top.c];
        prev->num_options = top.options;
        prev->new = true;
        prev->collapsed = false;
        if (prev->num_options != 0) {
            Tile *tmp = prev->options[0];
            prev->options[0] = prev->options[prev->num_options - 1];
            prev->options[prev->num_options - 1] = tmp;
            prev->num_options--;
        } else {
            cell_reset(prev);
        }

        heap_reset();

        if (prev->num_options != 0) {
            conflict = false;
        }

        propogate_options(grid, top.r, top.c, grid->rows * grid->cols);
    } else {
        /* Start over */
        for (int i = 0; i < grid->rows * grid->cols; i++) {
            cell_reset(&grid->cells[i]);
        }

        /* Pick a random cell and collapse it */
        int idx = rand() % (grid->rows * grid->cols);
        
        cell_collapse(&grid->cells[idx]);

        heap_reset();

        propogate_options(grid, idx / grid->rows, idx % grid->cols, depth);

        stack_size = 0;
    }
}

void wfc_draw(Texture texture) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();

    BeginDrawing();

    ClearBackground(BLACK);

    int img_width = tiles[0]->img.width;
    int img_height = tiles[0]->img.height;

    for (int i = 0; i < grid->rows; i++) {
        for (int j = 0; j < grid->cols; j++) {
            Cell *cell = &grid->cells[i * grid->cols + j];

            if (cell->collapsed && cell->new) {
                cell->new = false;
                Image img = cell->options[0]->img;
                Rectangle rect = {.x = img.width * j, .y = img.height * i, .width = img.width, .height = img.height};

                UpdateTextureRec(texture, rect, img.data);
            } else if (cell->new && cell->num_options == num_tiles) {
                Image img = GenImageColor(img_width, img_height, BLACK);
                ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
                Rectangle rect = {.x = img_width * j, .y = img_width * i, .width = img_width, .height = img_height};

                UpdateTextureRec(texture, rect, img.data);
            } else if (cell->new && cell->num_options > 0) {
                cell->new = false;
                Image combo = GenImageColor(img_width, img_height, BLACK);
                ImageFormat(&combo, PIXELFORMAT_UNCOMPRESSED_R8G8B8);

                float total_weight = 0.0;
                for (int k = 0; k < cell->num_options; k++) {
                    total_weight += cell->options[k]->frequency;
                }

                for (int k = 0; k < cell->num_options; k++) {
                    Image src_img = cell->options[k]->img;
                    uint8_t *src = (uint8_t *) src_img.data;
                    float rel = cell->options[k]->frequency / total_weight;
                    for (int pixel_i = 0; pixel_i < img_height; pixel_i++) {
                        for (int pixel_j = 0; pixel_j < img_width; pixel_j++) {
                            for (int ch = 0; ch < 3; ch++) {
                                int combo_idx = pixel_i * combo.width * 3 + pixel_j * 3 + ch;
                                int src_idx = pixel_i * src_img.width * 3 + pixel_j * 3 + ch;
                                ((uint8_t *) combo.data)[combo_idx] += src[src_idx] * rel;
                            }
                        }
                    }
                }

                Rectangle rect = {.x = img_width * j, .y = img_height * i, .width = img_width, .height = img_height};

                UpdateTextureRec(texture, rect, combo.data);
            }
        }
    }

    DrawTexturePro(
            texture, 
            (Rectangle) {.x = 0, .y = 0, .width = texture.width, .height = texture.height},
            (Rectangle) {.x = 0, .y = 0, .width = width, .height = height},
            (Vector2) {}, 
            0, 
            WHITE);

    EndDrawing();
}

int wfc_tile_width() {
    return tiles[0]->img.width;
}

int wfc_tile_height() {
    return tiles[0]->img.height;
}
