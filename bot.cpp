#include "bot.h"
#include "bsp.h"
#include "rps_internal.h"

/**
 * Handles bot write requests from TeenyAT.
 */
void bot_bus_write(teenyat* t, tny_uword addr, tny_word data, uint16_t* delay) {
    bot* current_bot = (bot*)(t->ex_data);

    switch (addr) {
    case MOVE_N: {
        if ((current_bot->y) <= 0) break;
        current_bot->y -= 1;
        break;
    }
    case MOVE_E: {
        if ((current_bot->x) >= g_grid_size - 2) break;
        current_bot->x += 1;
        break;
    }
    case MOVE_S: {
        if ((current_bot->y) >= g_grid_size - 2) break;
        current_bot->y += 1;
        break;
    }
    case MOVE_W: {
        if ((current_bot->x) <= 0) break;
        current_bot->x -= 1;
        break;
    }
    default:
        break;
    }

    if (addr == MOVE_N || addr == MOVE_E || addr == MOVE_S || addr == MOVE_W) {
        if (delay) {
            *delay += TNY_BUS_EXTERNAL_DELAY_ADJUST;
        }
    }
}

/**
 * Handles bot read requests from TeenyAT.
 */
void bot_bus_read(teenyat* t, tny_uword addr, tny_word* data, uint16_t* delay) {
    switch (addr) {
    // Current X coordinate.
    case X:
        data->u = ((bot*)(t->ex_data))->x;
        break;

    // Current Y coordinate.
    case Y:
        data->u = ((bot*)(t->ex_data))->y;
        break;

    // Nearest prey.
    case DETECT_PREY: {
        bot* active_bot = (bot*)(t->ex_data);

        BSPNode* current_node = g_bsp_root;
        while (current_node && (current_node->top_left || current_node->top_right || current_node->bottom_left || current_node->bottom_right)) {
            int mid_x = (current_node->x_min + current_node->x_max) / 2;
            int mid_y = (current_node->y_min + current_node->y_max) / 2;

            if (active_bot->x <= mid_x) {
                if (active_bot->y <= mid_y) {
                    current_node = current_node->top_left;
                } else {
                    current_node = current_node->bottom_left;
                }
            } else {
                if (active_bot->y <= mid_y) {
                    current_node = current_node->top_right;
                } else {
                    current_node = current_node->bottom_right;
                }
            }
        }

        bot* closest_bot = nullptr;
        int prey_nearby = 0;
        int lat_dir = 0;
        int hoz_dir = 0;

        for (bot* b : current_node->region_bots) {
            if (b->defeatedBy == active_bot->type) {
                if (closest_bot == nullptr) {
                    closest_bot = b;
                    if (b->y - active_bot->y < 0) lat_dir = 1;
                    else lat_dir = 0;
                    if (b->x - active_bot->x > 0) hoz_dir = 1;
                    else hoz_dir = 0;

                }
                else {
                    int curr_dx = closest_bot->x - active_bot->x;
                    int curr_dy = closest_bot->y - active_bot->y;
                    int new_dx = b->x - active_bot->x;
                    int new_dy = b->y - active_bot->y;

                    if (((new_dx * new_dx) + (new_dy * new_dy)) < ((curr_dx * curr_dx) + (curr_dy * curr_dy))) {
                        closest_bot = b;
                        if (new_dy < 0) lat_dir = 1;
                        else lat_dir = 0;
                        if (new_dx > 0) hoz_dir = 1;
                        else hoz_dir = 0;
                    }
                }
            }
        }

        if (closest_bot != nullptr) {
            prey_nearby = 1;
        }

        data->u = (hoz_dir << 2) | (lat_dir << 1) | (prey_nearby);
        break;
    }

    // Detect nearest predator (a bot that can defeat this bot).
    // Encodes presence and direction bits into data->u.
    case DETECT_PRED: {
        bot* active_bot = (bot*)(t->ex_data);

        BSPNode* current_node = g_bsp_root;
        while (current_node && (current_node->top_left || current_node->top_right || current_node->bottom_left || current_node->bottom_right)) {
            int mid_x = (current_node->x_min + current_node->x_max) / 2;
            int mid_y = (current_node->y_min + current_node->y_max) / 2;

            if (active_bot->x <= mid_x) {
                if (active_bot->y <= mid_y) {
                    current_node = current_node->top_left;
                } else {
                    current_node = current_node->bottom_left;
                }
            } else {
                if (active_bot->y <= mid_y) {
                    current_node = current_node->top_right;
                } else {
                    current_node = current_node->bottom_right; 
                }
            }
        }

        bot* closest_bot = nullptr;
        int pred_nearby = 0;
        int lat_dir = 0;
        int hoz_dir = 0;

        for (bot* b : current_node->region_bots) {
            if (b->defeats == active_bot->type) {
                if (closest_bot == nullptr) {
                    closest_bot = b;
                    if (b->y - active_bot->y < 0) lat_dir = 1;
                    else lat_dir = 0;
                    if (b->x - active_bot->x > 0) hoz_dir = 1;
                    else hoz_dir = 0;

                }
                else {
                    int curr_dx = closest_bot->x - active_bot->x;
                    int curr_dy = closest_bot->y - active_bot->y;
                    int new_dx = b->x - active_bot->x;
                    int new_dy = b->y - active_bot->y;

                    if (((new_dx * new_dx) + (new_dy * new_dy)) < ((curr_dx * curr_dx) + (curr_dy * curr_dy))) {
                        closest_bot = b;
                        if (new_dy < 0) lat_dir = 1;
                        else lat_dir = 0;
                        if (new_dx > 0) hoz_dir = 1;
                        else hoz_dir = 0;
                    }
                }
            }
        }

        if (closest_bot != nullptr) {
            pred_nearby = 1;
        }

        data->u = (hoz_dir << 2) | (lat_dir << 1) | (pred_nearby);
        break;
    }

    // Nearest ally.
    case DETECT_SELF: {
        bot* active_bot = (bot*)(t->ex_data);

        BSPNode* current_node = g_bsp_root;
        while (current_node && (current_node->top_left || current_node->top_right || current_node->bottom_left || current_node->bottom_right)) {
            int mid_x = (current_node->x_min + current_node->x_max) / 2;
            int mid_y = (current_node->y_min + current_node->y_max) / 2;

            if (active_bot->x <= mid_x) {
                if (active_bot->y <= mid_y) {
                    current_node = current_node->top_left; 
                } else {
                    current_node = current_node->bottom_left; 
                }
            } else {
                if (active_bot->y <= mid_y) {
                    current_node = current_node->top_right; 
                } else {
                    current_node = current_node->bottom_right; 
                }
            }
        }

        bot* closest_bot = nullptr;
        int self_nearby = 0;
        int lat_dir = 0;
        int hoz_dir = 0;

        for (bot* b : current_node->region_bots) {
            if ((b->type == active_bot->type) && b != active_bot) {
                if (closest_bot == nullptr) {
                    closest_bot = b;
                    if (b->y - active_bot->y < 0) lat_dir = 1;
                    else lat_dir = 0;
                    if (b->x - active_bot->x > 0) hoz_dir = 1;
                    else hoz_dir = 0;

                } 
                else {
                    int curr_dx = closest_bot->x - active_bot->x;
                    int curr_dy = closest_bot->y - active_bot->y;
                    int new_dx = b->x - active_bot->x;
                    int new_dy = b->y - active_bot->y;

                    if (((new_dx * new_dx) + (new_dy * new_dy)) < ((curr_dx * curr_dx) + (curr_dy * curr_dy))) {
                        closest_bot = b;
                        if (new_dy < 0) lat_dir = 1;
                        else lat_dir = 0;
                        if (new_dx > 0) hoz_dir = 1;
                        else hoz_dir = 0;
                    }
                }
            }
        }

        if (closest_bot != nullptr) {
            self_nearby = 1;
        }

        data->u = (hoz_dir << 2) | (lat_dir << 1) | (self_nearby);
        break;
    }

    default:
        break;
    }
}
