#include "bsp.h"

/**
 * Builds a BSP tree for spatial partitioning.
 */
BSPNode* buildBSP(std::vector<bot*>& region_bots, int x_min, int x_max, int y_min, int y_max, int max_bots_per_region) {
    if (region_bots.size() <= static_cast<decltype(region_bots.size())>(max_bots_per_region)) {
        return new BSPNode{x_min, x_max, y_min, y_max, region_bots};
    }

    int mid_x = (x_min + x_max) / 2;
    int mid_y = (y_min + y_max) / 2;

    std::vector<bot*> top_left_bots;
    std::vector<bot*> top_right_bots;
    std::vector<bot*> bottom_left_bots;
    std::vector<bot*> bottom_right_bots;

    for (bot* b : region_bots) {
        if (b->x <= mid_x && b->y <= mid_y) {
            top_left_bots.push_back(b);
        } else if (b->x > mid_x && b->y <= mid_y) {
            top_right_bots.push_back(b);
        } else if (b->x <= mid_x && b->y > mid_y) {
            bottom_left_bots.push_back(b);
        } else {
            bottom_right_bots.push_back(b);
        }
    }

    BSPNode* node = new BSPNode{x_min, x_max, y_min, y_max, {}};
    node->top_left = buildBSP(top_left_bots, x_min, mid_x, y_min, mid_y, max_bots_per_region);
    node->top_right = buildBSP(top_right_bots, mid_x, x_max, y_min, mid_y, max_bots_per_region);
    node->bottom_left = buildBSP(bottom_left_bots, x_min, mid_x, mid_y, y_max, max_bots_per_region);
    node->bottom_right = buildBSP(bottom_right_bots, mid_x, x_max, mid_y, y_max, max_bots_per_region);

    return node;
}
