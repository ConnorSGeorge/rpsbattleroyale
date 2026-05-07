#ifndef BSP_H
#define BSP_H

#include <vector>
#include "bot.h"

/**
 * A BSP tree node used for spatial partitioning.
 * The tree divides the grid into quadrants to speed up nearby-bot queries.
 */
struct BSPNode {
    int x_min, x_max, y_min, y_max;
    std::vector<bot*> region_bots;
    BSPNode* top_left = nullptr;
    BSPNode* top_right = nullptr;
    BSPNode* bottom_left = nullptr;
    BSPNode* bottom_right = nullptr;

    ~BSPNode() {
        delete top_left;
        delete top_right;
        delete bottom_left;
        delete bottom_right;
    }
};

/**
 * Builds a BSP tree for spatial partitioning.
 * Recursively divides the grid until each region contains fewer than
 * `max_bots_per_region` bots.
 */
BSPNode* buildBSP(std::vector<bot*>& region_bots, int x_min, int x_max, int y_min, int y_max, int max_bots_per_region);

#endif // BSP_H
