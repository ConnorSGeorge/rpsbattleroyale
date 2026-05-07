#include "rps_internal.h"

#include <algorithm>
#include <cstring>
#include <array>
#include <iomanip>
#include <random>
#include <sstream>

using namespace std;

/**
 * Maps team index to display name.
 */
static const char* team_name(int team) {
    if (team == 0) {
        return "Rock";
    }
    if (team == 1) {
        return "Paper";
    }
    return "Scissors";
}

/**
 * Formats a float using fixed precision.
 */
static string format_float_fixed(float value, int precision) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed, std::ios::floatfield);
    stream << std::setprecision(precision) << value;
    return stream.str();
}

/**
 * Constructs a game instance with clean initial runtime state.
 */
Game::Game()
        : bsp_root(nullptr),
            screen(nullptr),
            blank_sprite(nullptr),
            bot_count(0),
            bot_zero_count(0),
            bot_one_count(0),
            bot_two_count(0),
            playing(true) {
    memset(bot_sprites, 0, sizeof(bot_sprites));
    memset(team_kills, 0, sizeof(team_kills));
    memset(team_initial, 0, sizeof(team_initial));
    g_game_instance = this;
    g_grid_size = GRID_SIZE;
}

/**
 * Releases owned resources.
 */
Game::~Game() {
    cleanup();
}

/**
 * Initializes one match using three compiled bot binaries.
 */
bool Game::initialize(const char* rock_bin, const char* paper_bin, const char* scissors_bin) {
    rock_bin_path = rock_bin;
    paper_bin_path = paper_bin;
    scissors_bin_path = scissors_bin;
    playing = true;

    // Reset per-match state while preserving persistent resources like
    // window and loaded sprites (so restart does not close/reopen window).
    if (bsp_root) {
        delete bsp_root;
        bsp_root = nullptr;
        g_bsp_root = nullptr;
    }
    for (auto& b : bots) {
        if (b) {
            if (b->t) delete b->t;
            delete b;
        }
    }
    bots.clear();
    memset(team_kills, 0, sizeof(team_kills));

    // Initialize bot count
    bot_count = BOT_COUNT;
    bot_zero_count = BOTS_PER_TYPE;
    bot_one_count = BOTS_PER_TYPE;
    bot_two_count = BOTS_PER_TYPE;
    team_initial[0] = BOTS_PER_TYPE;
    team_initial[1] = BOTS_PER_TYPE;
    team_initial[2] = BOTS_PER_TYPE;

    // Create bot instances
    bots.resize(BOT_COUNT);
    for (int i = 0; i < BOT_COUNT; ++i) {
        bots[i] = new bot();
        bots[i]->t = nullptr;
    }

    // Load binary files
    const std::array<const char*, BOT_TYPES> bin_files_names = {rock_bin, paper_bin, scissors_bin};
    std::array<FILE*, BOT_TYPES> bin_files{};

    for (int i = 0; i < BOT_TYPES; ++i) {
        bin_files[i] = fopen(bin_files_names[i], "rb");
        if (!bin_files[i]) {
            for (int j = 0; j < i; ++j) {
                fclose(bin_files[j]);
            }
            return false;
        }
    }

    // Initialize bots with their TeenyAT instances
    for (int i = 0; i < BOT_TYPES; ++i) {
        for (int j = 0; j < BOTS_PER_TYPE; ++j) {
            const int id = i * BOTS_PER_TYPE + j;
            bots[id]->x = (rand() % (GRID_SIZE - 2)) + 1;
            bots[id]->y = (rand() % (GRID_SIZE - 2)) + 1;
            bots[id]->type = i;
            bots[id]->defeats = (i + 2) % BOT_TYPES;
            bots[id]->defeatedBy = (i + 1) % BOT_TYPES;

            teenyat* t = new teenyat();
            fseek(bin_files[i], 0, SEEK_SET);
            tny_init_from_file(t, bin_files[i], bot_bus_read, bot_bus_write);
            t->ex_data = (void*)(bots[id]);
            bots[id]->t = t;
        }
    }

    // Close binary files
    for (int i = 0; i < BOT_TYPES; ++i) {
        fclose(bin_files[i]);
    }

    if (!screen) {
        // Create fullscreen window while keeping a fixed game bitmap.
        // TIGR will center and scale the square render area, leaving black bars
        // where screen aspect ratio does not match.
        screen = tigrWindow(
            GRID_SIZE * CELL_SIZE,
            GRID_SIZE * CELL_SIZE,
            "RPS Battle Simulator",
            TIGR_FULLSCREEN
        );
        if (!screen) {
            return false;
        }
    }

    // Load sprites once (reuse across restarts).
    if (!bot_sprites[0]) bot_sprites[0] = tigrLoadImage("assets/rock.png");
    if (!bot_sprites[1]) bot_sprites[1] = tigrLoadImage("assets/paper.png");
    if (!bot_sprites[2]) bot_sprites[2] = tigrLoadImage("assets/scissors.png");
    if (!blank_sprite) blank_sprite = tigrLoadImage("assets/blank.png");

    // Initial render
    tigrFill(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE, BACKGROUND_COLOR);
    for (bot* b : bots) {
        draw_bot(b);
    }
    tigrUpdate(screen);

    return true;
}

/**
 * Clears a single grid cell using blank sprite or fallback fill.
 */
void Game::clear_cell(int x, int y) {
    int px = x * CELL_SIZE;
    int py = y * CELL_SIZE;
    if (blank_sprite) {
        tigrBlitAlpha(screen, blank_sprite, px, py, 0, 0, CELL_SIZE, CELL_SIZE, 1.0f);
    } else {
        tigrFill(screen, px, py, CELL_SIZE, CELL_SIZE, BACKGROUND_COLOR);
    }
}

/**
 * Draws one bot sprite at its current grid position.
 */
void Game::draw_bot(const bot* b) {
    if (!b) return;

    int px = b->x * CELL_SIZE;
    int py = b->y * CELL_SIZE;

    if (b->type >= 0 && b->type < 3 && bot_sprites[b->type]) {
        Tigr* sprite = bot_sprites[b->type];
        tigrBlitAlpha(screen, sprite, px, py, 0, 0, CELL_SIZE, CELL_SIZE, 1.0f);
    }
}

/**
 * Resolves battles for bots in the BSP tree regions and updates match state.
 */
void Game::perform_battles(BSPNode* node) {
    if (!node) return;

    if ((bot_count < END_CONDITION) || (bot_zero_count <= 0) || (bot_one_count <= 0) || (bot_two_count <= 0)) {
        playing = false;
        return;
    }

    for (bot* b1 : node->region_bots) {
        for (bot* b2 : node->region_bots) {
            if (b1->x == b2->x && b1->y == b2->y && b1 != b2) {
                if (b1->defeats == b2->type) {
                    if (b2->type == 0) bot_zero_count--;
                    else if (b2->type == 1) bot_one_count--;
                    else if (b2->type == 2) bot_two_count--;

                    if (b1->type >= 0 && b1->type < 3) team_kills[b1->type]++;

                    clear_cell(b2->x, b2->y);
                    
                    draw_bot(b1);
                    bot_count--;

                    bots.erase(remove(bots.begin(), bots.end(), b2), bots.end());
                    node->region_bots.erase(remove(node->region_bots.begin(), node->region_bots.end(), b2), node->region_bots.end());
                }
            }
        }
    }

    perform_battles(node->top_left);
    perform_battles(node->top_right);
    perform_battles(node->bottom_left);
    perform_battles(node->bottom_right);
}

/**
 * Shows end-of-match statistics and returns whether restart was requested.
 */
bool Game::show_final_stats() {
    int winner_type = 0;
    int winner_count = bot_zero_count;
    if (bot_one_count > winner_count) {
        winner_type = 1;
        winner_count = bot_one_count;
    }
    if (bot_two_count > winner_count) {
        winner_type = 2;
        winner_count = bot_two_count;
    }

    const int team_left[3] = {bot_zero_count, bot_one_count, bot_two_count};
    const int team_deaths[3] = {
        team_initial[0] - team_left[0],
        team_initial[1] - team_left[1],
        team_initial[2] - team_left[2]
    };

    const bool winner_tie = (count(begin(team_left), end(team_left), winner_count) > 1);

    while (!tigrClosed(screen)) {
        tigrFill(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE, UI_BACKGROUND_COLOR);

        int y = 30;
        draw_text(screen, 20, y, UI_TEXT_COLOR, "RPS Battle Results");
        y += 50;

        draw_text(screen, 20, y, UI_TEXT_COLOR, "Bots left at end:");
        y += 30;
        draw_text(screen, 40, y, UI_TEXT_COLOR, "Rock: " + to_string(bot_zero_count));
        y += 25;
        draw_text(screen, 40, y, UI_TEXT_COLOR, "Paper: " + to_string(bot_one_count));
        y += 25;
        draw_text(screen, 40, y, UI_TEXT_COLOR, "Scissors: " + to_string(bot_two_count));
        y += 40;

        draw_text(screen, 20, y, UI_TEXT_COLOR, "Team kills:");
        y += 30;
        draw_text(screen, 40, y, UI_TEXT_COLOR, "Rock: " + to_string(team_kills[0]));
        y += 25;
        draw_text(screen, 40, y, UI_TEXT_COLOR, "Paper: " + to_string(team_kills[1]));
        y += 25;
        draw_text(screen, 40, y, UI_TEXT_COLOR, "Scissors: " + to_string(team_kills[2]));
        y += 40;

        draw_text(screen, 20, y, UI_TEXT_COLOR, "Survival % by team:");
        y += 30;
        for (int i = 0; i < 3; i++) {
            float survival_pct = 0.0f;
            if (team_initial[i] > 0) {
                survival_pct = (100.0f * (float)team_left[i]) / (float)team_initial[i];
            }
            draw_text(screen, 40, y, UI_TEXT_COLOR, string(team_name(i)) + ": " + format_float_fixed(survival_pct, 1) + "%");
            y += 25;
        }
        y += 15;

        draw_text(screen, 20, y, UI_TEXT_COLOR, "Kill/Death ratio by team:");
        y += 30;
        for (int i = 0; i < 3; i++) {
            if (team_deaths[i] <= 0) {
                draw_text(screen, 40, y, UI_TEXT_COLOR, string(team_name(i)) + ": INF");
            } else {
                float kd = (float)team_kills[i] / (float)team_deaths[i];
                draw_text(screen, 40, y, UI_TEXT_COLOR, string(team_name(i)) + ": " + format_float_fixed(kd, 2));
            }
            y += 25;
        }
        y += 15;

        if (winner_tie) {
            draw_text(screen, 20, y, UI_TEXT_COLOR, "Winner: Tie");
        } else {
            draw_text(screen, 20, y, UI_TEXT_COLOR, "Winner: " + string(team_name(winner_type)));
        }
        y += 30;
        draw_text(screen, 20, y, UI_TEXT_COLOR, "Total bots left: " + to_string(bot_count));
        y += 40;

        draw_text(screen, 20, y, UI_TEXT_COLOR, "Press ENTER to play again");
        y += 25;
        draw_text(screen, 20, y, UI_TEXT_COLOR, "Press ESC to return to the previous menu");

        tigrUpdate(screen);
        if (tigrKeyDown(screen, TK_RETURN) || tigrKeyDown(screen, TK_PADENTER)) {
            tigrFill(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE, UI_BACKGROUND_COLOR);
            tigrUpdate(screen);
            return true;
        }
        if (tigrKeyDown(screen, TK_ESCAPE)) {
            tigrFill(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE, UI_BACKGROUND_COLOR);
            tigrUpdate(screen);
            return false;
        }
    }

    return false;
}

/**
 * Runs match simulation loop and handles restart flow between rounds.
 */
bool Game::run() {
    while (true) {
        const int maxcutoff = static_cast<int>(BOT_COUNT / 1.5);
        int stalled_rounds = 0;
        static int frame_skip = 0;

        while (playing) {
            if (tigrClosed(screen)) {
                playing = false;
                return false;
            }

            random_device rd;
            mt19937 g(rd());
            shuffle(bots.begin(), bots.end(), g);

            tigrFill(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE, BACKGROUND_COLOR);

            for (bot* b : bots) {
                draw_bot(b);
            }

            g_bsp_root = buildBSP(bots, 0, GRID_SIZE, 0, GRID_SIZE, maxcutoff);
            bsp_root = g_bsp_root;

            for (bot* b : bots) {
                tny_clock(b->t);
            }

            int before_battles = bot_count;
            perform_battles(bsp_root);

            if (bot_count == before_battles) {
                stalled_rounds++;
            } else {
                stalled_rounds = 0;
            }

            if (stalled_rounds >= MAX_STALLED_ROUNDS) {
                playing = false;
            }

            if (frame_skip % 3 == 0) {
                tigrUpdate(screen);
            }
            frame_skip++;
        }

        if (tigrClosed(screen)) {
            return false;
        }

        bool restart_requested = show_final_stats();
        if (tigrClosed(screen)) {
            return false;
        }

        if (!restart_requested) {
            return true;
        }

        if (!initialize(rock_bin_path.c_str(), paper_bin_path.c_str(), scissors_bin_path.c_str())) {
            return true;
        }
    }

    return true;
}

/**
 * Frees allocated simulation resources and window assets.
 */
void Game::cleanup() {
    if (bsp_root) {
        delete bsp_root;
        bsp_root = nullptr;
        g_bsp_root = nullptr;
    }

    for (auto& b : bots) {
        if (b) {
            if (b->t) delete b->t;
            delete b;
        }
    }
    bots.clear();

    for (int i = 0; i < 3; i++) {
        if (bot_sprites[i]) {
            tigrFree(bot_sprites[i]);
            bot_sprites[i] = nullptr;
        }
    }
    if (blank_sprite) {
        tigrFree(blank_sprite);
        blank_sprite = nullptr;
    }

    if (screen) {
        tigrFree(screen);
        screen = nullptr;
    }
}

// Static bus handler wrappers
/**
 * Static wrapper that forwards VM bus reads to bot handler.
 */
void Game::bus_read(teenyat *t, tny_uword addr, tny_word *data, uint16_t *delay) {
    bot_bus_read(t, addr, data, delay);
}

/**
 * Static wrapper that forwards VM bus writes to bot handler.
 */
void Game::bus_write(teenyat *t, tny_uword addr, tny_word data, uint16_t *delay) {
    bot_bus_write(t, addr, data, delay);
}
