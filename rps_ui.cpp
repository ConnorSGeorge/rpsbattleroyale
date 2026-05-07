#include "rps_internal.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <sstream>

using namespace std;

// Shared runtime state used by the UI and simulation code.
BSPNode* g_bsp_root = nullptr;
int g_grid_size = 0;
Game* g_game_instance = nullptr;

// Local UI helpers.
static string mask_text(const string& value) {
    return string(value.size(), '*');
}

static bool is_printable_ascii(int ch) {
    return ch >= 32 && ch <= 126;
}

static bool parse_positive_long_long(const string& text, long long& outValue) {
    if (text.empty()) {
        return false;
    }

    char* end_ptr = nullptr;
    long long value = std::strtoll(text.c_str(), &end_ptr, 10);
    if (end_ptr == text.c_str() || (end_ptr && *end_ptr != '\0') || value <= 0) {
        return false;
    }

    outValue = value;
    return true;
}

static bool contains_bot_version_id(const vector<BotVersionSummary>& rows, long long botVersionId) {
    for (const auto& row : rows) {
        if (row.botVersionId == botVersionId) {
            return true;
        }
    }
    return false;
}

// Validates credentials and updates the cached username on success.
static bool attempt_login(DatabaseClient& db_client,
                          string& username,
                          string& password,
                          long long& outUserId,
                          string& outUsername,
                          string& outError) {
    if (username.empty() || password.empty()) {
        outError = "Enter both username and password.";
        return false;
    }

    if (!db_client.authenticateUser(username, password, outUserId, outError)) {
        password.clear();
        return false;
    }

    outUsername = username;
    return true;
}

static string& slot_text_by_index(array<string, 3>& slotTexts, int index) {
    return slotTexts[static_cast<size_t>(index)];
}

static bool parse_selected_bot_ids(const array<string, 3>& slotTexts, array<long long, 3>& outIds) {
    return parse_positive_long_long(slotTexts[0], outIds[0]) &&
           parse_positive_long_long(slotTexts[1], outIds[1]) &&
           parse_positive_long_long(slotTexts[2], outIds[2]);
}

static bool selected_ids_are_accessible(const vector<BotVersionSummary>& options, const array<long long, 3>& selectedIds) {
    return contains_bot_version_id(options, selectedIds[0]) &&
           contains_bot_version_id(options, selectedIds[1]) &&
           contains_bot_version_id(options, selectedIds[2]);
}

static constexpr int UI_TEXT_TRACKING = 2;

// Draws a raw string without extra spacing.
static void draw_text_raw(Tigr* screen, int x, int y, TPixel color, const string& text) {
    tigrPrint(screen, tfont, x, y, color, "%s", text.c_str());
}

static int measure_text_width_spaced(const string& text, int tracking) {
    if (tracking <= 0) {
        return tigrTextWidth(tfont, text.c_str());
    }

    if (text.empty()) {
        return 0;
    }

    int width = 0;
    for (size_t index = 0; index < text.size(); ++index) {
        char ch = text[index];
        char glyph[2] = {ch, '\0'};
        width += tigrTextWidth(tfont, glyph);
        if (index + 1 < text.size()) {
            width += tracking;
        }
    }

    return width;
}

static void draw_text_spaced(Tigr* screen, int x, int y, TPixel color, const string& text, int tracking) {
    if (tracking <= 0 || text.empty()) {
        draw_text_raw(screen, x, y, color, text);
        return;
    }

    int cursor_x = x;
    for (size_t index = 0; index < text.size(); ++index) {
        char ch = text[index];
        char glyph[2] = {ch, '\0'};
        draw_text_raw(screen, cursor_x, y, color, glyph);
        cursor_x += tigrTextWidth(tfont, glyph);
        if (index + 1 < text.size()) {
            cursor_x += tracking;
        }
    }
}

void draw_centered_text(Tigr* screen, int y, TPixel color, const string& text) {
    int w = measure_text_width_spaced(text, UI_TEXT_TRACKING);
    int x = max(20, (screen->w - w) / 2);
    draw_text_spaced(screen, x, y, color, text, UI_TEXT_TRACKING);
}

void draw_text(Tigr* screen, int x, int y, TPixel color, const string& text) {
    draw_text_spaced(screen, x, y, color, text, UI_TEXT_TRACKING);
}

void draw_centered_in_box(Tigr* screen, int x, int y, int w, int h, TPixel color, const string& text) {
    int text_w = measure_text_width_spaced(text, UI_TEXT_TRACKING);
    int text_h = tigrTextHeight(tfont, text.c_str());
    int tx = x + max(0, (w - text_w) / 2);
    int ty = y + max(0, (h - text_h) / 2);
    draw_text(screen, tx, ty, color, text);
}

vector<string> split_lines_text(const string& text) {
    vector<string> lines;
    istringstream stream(text);
    string line;
    while (getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

bool Game::ensure_window() {
    if (screen) {
        return true;
    }

    screen = tigrWindow(
        GRID_SIZE * CELL_SIZE,
        GRID_SIZE * CELL_SIZE,
        "RPS: Battle Royale",
        TIGR_FULLSCREEN
    );

    return screen != nullptr;
}

void Game::draw_label(int x, int y, const string& text, TPixel color) {
    draw_text(screen, x, y, color, text);
}

void Game::draw_input_box(int x, int y, int w, const string& label, const string& value, bool active, bool masked) {
    TPixel border = active ? tigrRGB(90, 160, 255) : tigrRGB(140, 140, 140);
    TPixel label_color = UI_TEXT_COLOR;
    TPixel value_color = UI_TEXT_COLOR;

    draw_label(x, y - 18, label, label_color);
    tigrFillRect(screen, x, y, w, 30, tigrRGB(18, 18, 18));
    tigrRect(screen, x, y, w, 30, border);
    draw_text(screen, x + 8, y + 8, value_color, masked ? mask_text(value) : value);

    if (active) {
        int caret_x = x + 8 + measure_text_width_spaced((masked ? mask_text(value) : value), UI_TEXT_TRACKING) + 1;
        tigrLine(screen, caret_x, y + 6, caret_x, y + 24, value_color);
    }
}

void Game::draw_bot_option_row(int x, int y, const BotVersionSummary& row, bool highlighted) {
    TPixel color = UI_TEXT_COLOR;
    TPixel accent = highlighted ? tigrRGB(35, 70, 120) : tigrRGB(18, 18, 18);
    tigrFillRect(screen, x, y, 640, 22, accent);

    string owner_tag = row.isOwner ? "mine" : "shared";
    string text = to_string(row.botVersionId) + "  |  " + row.ownerUsername + "  |  " + row.botName + "  |  " + owner_tag;
    draw_text(screen, x + 8, y + 4, color, text);
}

bool Game::show_message_screen(const string& title, const vector<string>& lines) {
    if (!ensure_window()) {
        return false;
    }

    while (!tigrClosed(screen)) {
        tigrFill(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE + 1, UI_BACKGROUND_COLOR);

        draw_centered_text(screen, 30, UI_TEXT_COLOR, title);

        int y = 90;
        for (const auto& line : lines) {
            draw_centered_text(screen, y, UI_TEXT_COLOR, line);
            y += 26;
        }

        draw_centered_text(screen, GRID_SIZE * CELL_SIZE - 70, UI_TEXT_COLOR, "Press Enter to continue, Escape to quit");
        tigrUpdate(screen);

        if (tigrKeyDown(screen, TK_RETURN)) {
            return true;
        }
        if (tigrKeyDown(screen, TK_ESCAPE)) {
            return false;
        }
    }

    return false;
}

bool Game::show_upload_result_screen(const string& botName, long long botVersionId, const string& visibility, bool success, const vector<string>& summaryLines, const string& compileLog) {
    if (!ensure_window()) {
        return false;
    }

    vector<string> log_lines = split_lines_text(compileLog);
    size_t scroll = 0;
    const size_t visible_log_lines = 10;

    while (!tigrClosed(screen)) {
        tigrFill(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE + 1, UI_BACKGROUND_COLOR);

        draw_centered_text(screen, 22, UI_TEXT_COLOR, success ? "Upload Complete" : "Upload Failed");
        draw_centered_text(screen, 46, UI_TEXT_COLOR, "Bot: " + botName + "   |   Version ID: " + to_string(botVersionId) + "   |   Visibility: " + visibility);

        int y = 86;
        for (const auto& line : summaryLines) {
            draw_centered_text(screen, y, UI_TEXT_COLOR, line);
            y += 24;
        }

        draw_text(screen, 40, 186, UI_TEXT_COLOR, "Compiler Output:");

        if (log_lines.empty()) {
            draw_text(screen, 52, 220, UI_TEXT_COLOR, "(no compiler output)");
        } else {
            size_t max_scroll = (log_lines.size() > visible_log_lines) ? (log_lines.size() - visible_log_lines) : 0;
            if (scroll > max_scroll) {
                scroll = max_scroll;
            }

            int log_y = 218;
            for (size_t i = scroll; i < log_lines.size() && i < scroll + visible_log_lines; ++i) {
                draw_text(screen, 52, log_y, UI_TEXT_COLOR, log_lines[i]);
                log_y += 16;
            }

            draw_text(screen, 52, 380, UI_TEXT_COLOR, "PageUp/PageDown scroll the output");
        }

        draw_text(screen, 40, 398, UI_TEXT_COLOR, success ? "Press Enter to continue." : "Press Enter to return and fix the issue.");
        draw_text(screen, 40, 418, UI_TEXT_COLOR, "Escape returns to the previous screen.");

        tigrUpdate(screen);

        if (tigrKeyDown(screen, TK_ESCAPE) || tigrKeyDown(screen, TK_RETURN)) {
            return success;
        }

        if (!log_lines.empty()) {
            size_t max_scroll = (log_lines.size() > visible_log_lines) ? (log_lines.size() - visible_log_lines) : 0;
            if (tigrKeyDown(screen, TK_PAGEUP) && scroll > 0) {
                scroll--;
            }
            if (tigrKeyDown(screen, TK_PAGEDN) && scroll < max_scroll) {
                scroll++;
            }
        }
    }

    return false;
}

bool Game::show_create_account_screen(DatabaseClient& db_client, long long& outUserId, string& outUsername) {
    if (!ensure_window()) {
        return false;
    }

    string username;
    string password;
    string confirm_password;
    string error_message;
    int active_field = 0;

    while (!tigrClosed(screen)) {
        tigrFill(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE + 1, UI_BACKGROUND_COLOR);

        draw_centered_text(screen, 24, UI_TEXT_COLOR, "Create Account");
        draw_centered_text(screen, 50, UI_TEXT_COLOR, "Enter a username and password, then press Enter to create it.");
        draw_centered_text(screen, 72, UI_TEXT_COLOR, "Arrow keys switch fields. Backspace deletes text. Escape quits.");

        draw_input_box(140, 130, 360, "Username", username, active_field == 0, false);
        draw_input_box(140, 190, 360, "Password", password, active_field == 1, true);
        draw_input_box(140, 250, 360, "Confirm Password", confirm_password, active_field == 2, true);

        if (!error_message.empty()) {
            draw_text(screen, 140, 310, UI_TEXT_COLOR, error_message);
        }

        tigrUpdate(screen);

        if (tigrKeyDown(screen, TK_ESCAPE)) {
            return false;
        }

        if (tigrKeyDown(screen, TK_LEFT) || tigrKeyDown(screen, TK_UP)) {
            active_field = (active_field + 2) % 3;
        }
        if (tigrKeyDown(screen, TK_RIGHT) || tigrKeyDown(screen, TK_DOWN)) {
            active_field = (active_field + 1) % 3;
        }

        if (tigrKeyDown(screen, TK_BACKSPACE)) {
            string* target = (active_field == 0) ? &username : (active_field == 1 ? &password : &confirm_password);
            if (!target->empty()) {
                target->pop_back();
            }
        }

        int ch = tigrReadChar(screen);
        if (is_printable_ascii(ch)) {
            string* target = (active_field == 0) ? &username : (active_field == 1 ? &password : &confirm_password);
            target->push_back(static_cast<char>(ch));
        }

        if (tigrKeyDown(screen, TK_RETURN)) {
            if (username.empty() || password.empty()) {
                error_message = "Username and password are required.";
                continue;
            }
            if (password != confirm_password) {
                error_message = "Passwords do not match.";
                continue;
            }

            if (!db_client.createUser(username, password, outUserId, error_message)) {
                continue;
            }

            outUsername = username;
            return true;
        }
    }

    return false;
}

bool Game::show_login_screen(DatabaseClient& db_client, long long& outUserId, string& outUsername, bool& outCreateAccountRequested) {
    if (!ensure_window()) {
        return false;
    }

    outCreateAccountRequested = false;
    string username;
    string password;
    string error_message;
    int focus_item = 0; // 0=username, 1=password, 2=login, 3=create account, 4=quit

    while (!tigrClosed(screen)) {
        tigrFill(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE + 1, UI_BACKGROUND_COLOR);

        draw_centered_text(screen, 24, UI_TEXT_COLOR, "RPS Login");
        draw_centered_text(screen, 50, UI_TEXT_COLOR, "Enter your username and password to continue.");
        draw_centered_text(screen, 72, UI_TEXT_COLOR, "Arrow keys switch focus. Enter activates the selected field or button.");

        int column_x = 150;
        int column_w = 340;

        draw_input_box(column_x, 130, column_w, "Username", username, focus_item == 0, false);
        draw_input_box(column_x, 190, column_w, "Password", password, focus_item == 1, true);

        draw_text(screen, column_x, 250, UI_TEXT_COLOR, "Password is hidden as you type.");
        if (!error_message.empty()) {
            draw_text(screen, column_x, 280, UI_TEXT_COLOR, error_message);
        }

        int button_x = column_x;
        int login_y = 330;
        int create_y = 376;
        int quit_y = 422;
        int button_w = column_w;
        int button_h = 34;

        TPixel login_fill = (focus_item == 2) ? tigrRGB(35, 70, 120) : tigrRGB(20, 20, 20);
        TPixel login_border = (focus_item == 2) ? tigrRGB(90, 160, 255) : tigrRGB(120, 120, 120);
        TPixel create_fill = (focus_item == 3) ? tigrRGB(35, 70, 120) : tigrRGB(20, 20, 20);
        TPixel create_border = (focus_item == 3) ? tigrRGB(90, 160, 255) : tigrRGB(120, 120, 120);
        TPixel quit_fill = (focus_item == 4) ? tigrRGB(90, 40, 40) : tigrRGB(20, 20, 20);
        TPixel quit_border = (focus_item == 4) ? tigrRGB(255, 160, 160) : tigrRGB(120, 120, 120);

        tigrFillRect(screen, button_x, login_y, button_w, button_h, login_fill);
        tigrRect(screen, button_x, login_y, button_w, button_h, login_border);
        draw_centered_in_box(screen, button_x, login_y, button_w, button_h, UI_TEXT_COLOR, "Login");

        tigrFillRect(screen, button_x, create_y, button_w, button_h, create_fill);
        tigrRect(screen, button_x, create_y, button_w, button_h, create_border);
        draw_centered_in_box(screen, button_x, create_y, button_w, button_h, UI_TEXT_COLOR, "Create Account");

        tigrFillRect(screen, button_x, quit_y, button_w, button_h, quit_fill);
        tigrRect(screen, button_x, quit_y, button_w, button_h, quit_border);
        draw_centered_in_box(screen, button_x, quit_y, button_w, button_h, UI_TEXT_COLOR, "Quit");

        tigrUpdate(screen);

        int ch = tigrReadChar(screen);

        if (tigrKeyDown(screen, TK_LEFT) || tigrKeyDown(screen, TK_UP)) {
            focus_item = (focus_item + 4) % 5;
        }
        if (tigrKeyDown(screen, TK_RIGHT) || tigrKeyDown(screen, TK_DOWN)) {
            focus_item = (focus_item + 1) % 5;
        }

        if (tigrKeyDown(screen, TK_BACKSPACE)) {
            if (focus_item == 0) {
                if (!username.empty()) {
                    username.pop_back();
                }
            } else if (focus_item == 1) {
                if (!password.empty()) {
                    password.pop_back();
                }
            }
        }

        if (tigrKeyDown(screen, TK_RETURN)) {
            if (focus_item == 0) {
                focus_item = 1;
                continue;
            }
            if (focus_item == 1) {
                focus_item = 2;
                continue;
            }

            if (focus_item == 2) {
                if (attempt_login(db_client, username, password, outUserId, outUsername, error_message)) {
                    return true;
                }
                continue;
            }
            if (focus_item == 3) {
                outCreateAccountRequested = true;
                return false;
            }
            outCreateAccountRequested = false;
            return false;
        }

        if (is_printable_ascii(ch)) {
            if (focus_item == 0) {
                username.push_back(static_cast<char>(ch));
            } else if (focus_item == 1) {
                password.push_back(static_cast<char>(ch));
            }
        }
    }

    return false;
}

bool Game::show_bot_selection_screen(DatabaseClient& db_client, long long userId, const string& username, vector<BotVersionSummary>& outOptions, long long& outRockId, long long& outPaperId, long long& outScissorsId) {
    if (!ensure_window()) {
        return false;
    }

    (void)db_client;
    (void)userId;
    (void)username;

    array<string, 3> slot_texts;
    const char* slot_labels[3] = {
        "Rock bot_version_id",
        "Paper bot_version_id",
        "Scissors bot_version_id"
    };
    const int slot_y[3] = {430, 490, 550};

    int active_field = 0;
    size_t page = 0;
    const size_t per_page = 12;
    string error_message;

    while (!tigrClosed(screen)) {
        tigrFill(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE + 1, UI_BACKGROUND_COLOR);

        draw_centered_text(screen, 16, UI_TEXT_COLOR, "Choose three compiled bot versions");
        draw_centered_text(screen, 38, UI_TEXT_COLOR, "Type a version ID from the list for each slot; Up/Down switches slots; Enter confirms.");
        draw_centered_text(screen, 58, UI_TEXT_COLOR, "Left/Right browses pages. Backspace deletes in active slot. Shared bots are public/unlisted.");

        draw_text(screen, 24, 86, UI_TEXT_COLOR, "Available bot versions:");

        size_t page_count = (outOptions.size() + per_page - 1) / per_page;
        if (page_count == 0) page_count = 1;
        if (page >= page_count) page = page_count - 1;

        size_t start = page * per_page;
        size_t end = min(start + per_page, outOptions.size());
        int row_y = 110;
        for (size_t i = start; i < end; ++i) {
            draw_bot_option_row(24, row_y, outOptions[i], false);
            row_y += 24;
        }

        draw_text(screen, 24, 400, UI_TEXT_COLOR, "Slot inputs:");
        for (int i = 0; i < 3; ++i) {
            draw_input_box(24,
                           slot_y[i],
                           180,
                           slot_labels[i],
                           slot_text_by_index(slot_texts, i),
                           active_field == i,
                           false);
        }

        draw_text(screen, 240, 430, UI_TEXT_COLOR, "Use the IDs shown above.");
        draw_text(screen, 240, 450, UI_TEXT_COLOR, "You can choose your own bots or shared bots.");
        draw_text(screen, 240, 470, UI_TEXT_COLOR, "Duplicate IDs are allowed if you want the same bot more than once.");

        draw_text(screen, 240, 520, UI_TEXT_COLOR, "Page " + to_string(page + 1) + " of " + to_string(page_count));

        if (!error_message.empty()) {
            draw_text(screen, 240, 560, UI_TEXT_COLOR, error_message);
        }

        tigrUpdate(screen);

        if (tigrKeyDown(screen, TK_ESCAPE)) {
            return false;
        }

        if (tigrKeyDown(screen, TK_UP)) {
            active_field = (active_field + 2) % 3;
        }
        if (tigrKeyDown(screen, TK_DOWN)) {
            active_field = (active_field + 1) % 3;
        }

        if (tigrKeyDown(screen, TK_LEFT) && page > 0) {
            page--;
        }
        if (tigrKeyDown(screen, TK_RIGHT) && (page + 1) < page_count) {
            page++;
        }

        if (tigrKeyDown(screen, TK_BACKSPACE)) {
            string& target = slot_text_by_index(slot_texts, active_field);
            if (!target.empty()) {
                target.pop_back();
            }
        }

        int ch = tigrReadChar(screen);
        if (ch >= '0' && ch <= '9') {
            string& target = slot_text_by_index(slot_texts, active_field);
            target.push_back(static_cast<char>(ch));
        }

        if (tigrKeyDown(screen, TK_RETURN)) {
            array<long long, 3> selected_ids = {0, 0, 0};

            if (!parse_selected_bot_ids(slot_texts, selected_ids)) {
                error_message = "Enter positive numeric bot_version IDs for all three slots.";
                continue;
            }

            if (!selected_ids_are_accessible(outOptions, selected_ids)) {
                error_message = "One or more selected IDs are not in your accessible bot list.";
                continue;
            }

            outRockId = selected_ids[0];
            outPaperId = selected_ids[1];
            outScissorsId = selected_ids[2];
            return true;
        }
    }

    return false;
}
