#ifndef RPS_INTERNAL_H
#define RPS_INTERNAL_H

#include "rps.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

extern BSPNode* g_bsp_root;
extern int g_grid_size;
extern Game* g_game_instance;

// TeenyAT bus hooks.
void bot_bus_read(teenyat* t, tny_uword addr, tny_word* data, uint16_t* delay);
void bot_bus_write(teenyat* t, tny_uword addr, tny_word data, uint16_t* delay);

// Shared text helpers.
std::vector<std::string> split_lines_text(const std::string& text);
void draw_centered_text(Tigr* screen, int y, TPixel color, const std::string& text);
void draw_text(Tigr* screen, int x, int y, TPixel color, const std::string& text);
void draw_centered_in_box(Tigr* screen, int x, int y, int w, int h, TPixel color, const std::string& text);

#endif // RPS_INTERNAL_H
