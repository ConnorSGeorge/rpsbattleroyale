#ifndef RPS_H
#define RPS_H

#include <vector>
#include <string>
#include "tigr.h"
#include "bot.h"
#include "bsp.h"
#include "db.h"

/**
 * `Game` encapsulates the RPS battle simulation.
 * It manages match state, bot setup, rendering, and the main flow.
 */
class Game {
private:
    static constexpr int GRID_SIZE = 60;
    static constexpr int CELL_SIZE = 16;
    static constexpr int BOT_COUNT = 210;
    static constexpr int BOT_TYPES = 3;
    static constexpr int BOTS_PER_TYPE = BOT_COUNT / BOT_TYPES;
    static constexpr int MAX_STALLED_ROUNDS = 300;
    static constexpr int END_CONDITION = 10;
    static constexpr TPixel UI_BACKGROUND_COLOR = {0, 0, 0, 255};
    static constexpr TPixel UI_TEXT_COLOR = {255, 255, 255, 255};
    static constexpr TPixel BACKGROUND_COLOR = {34, 139, 34, 255};

    std::vector<bot*> bots;
    BSPNode* bsp_root;
    Tigr* screen;
    Tigr* bot_sprites[3];
    Tigr* blank_sprite;

    int bot_count;
    int bot_zero_count;
    int bot_one_count;
    int bot_two_count;
    int team_kills[3];
    int team_initial[3];
    bool playing;
    std::string rock_bin_path;
    std::string paper_bin_path;
    std::string scissors_bin_path;

    void clear_cell(int x, int y);
    void draw_bot(const bot* b);
    void perform_battles(BSPNode* node);
    bool show_final_stats();
    void cleanup();

public:
    Game();
    ~Game();
    
    /**
     * Initializes the game with compiled bot binaries.
     * @param rock_bin Path to Rock bot binary
     * @param paper_bin Path to Paper bot binary
     * @param scissors_bin Path to Scissors bot binary
     * @return true on success, false on failure
     */
    bool initialize(const char* rock_bin, const char* paper_bin, const char* scissors_bin);

    /**
     * Runs the full database-driven flow:
     * start menu -> login/create account -> bot selection screen -> match launch
     */
    bool run_database_mode();
    
    /**
     * Runs the main game loop.
     */
    bool run();
    
    // Static bus handlers for the TeenyAT VM.
    static void bus_read(teenyat* t, tny_uword addr, tny_word* data, uint16_t* delay);
    static void bus_write(teenyat* t, tny_uword addr, tny_word data, uint16_t* delay);

private:
    bool show_message_screen(const std::string& title, const std::vector<std::string>& lines);
    bool show_upload_result_screen(const std::string& botName, long long botVersionId, const std::string& visibility, bool success, const std::vector<std::string>& summaryLines, const std::string& compileLog);
    bool show_create_account_screen(DatabaseClient& db_client, long long& outUserId, std::string& outUsername);
    bool show_login_screen(DatabaseClient& db_client, long long& outUserId, std::string& outUsername, bool& outCreateAccountRequested);
    enum class UploadScreenResult { ExitApplication, ReturnToLogin, ContinueFlow };
    UploadScreenResult show_upload_assembly_screen(DatabaseClient& db_client, long long userId, const std::string& username);
    bool show_bot_selection_screen(DatabaseClient& db_client, long long userId, const std::string& username, std::vector<BotVersionSummary>& outOptions, long long& outRockId, long long& outPaperId, long long& outScissorsId);
    enum class AuthFlowResult { ExitApplication, RestartFlow, MatchCompleted };
    AuthFlowResult run_authenticated_flow(DatabaseClient& db_client, long long userId, const std::string& username, std::string& db_error);
    bool fetch_selected_binaries(DatabaseClient& db_client, long long rock_id, long long paper_id, long long scissors_id, std::vector<unsigned char>& rock_bytes, std::vector<unsigned char>& paper_bytes, std::vector<unsigned char>& scissors_bytes, std::string& db_error);
    void draw_label(int x, int y, const std::string& text, TPixel color);
    void draw_input_box(int x, int y, int w, const std::string& label, const std::string& value, bool active, bool masked);
    void draw_bot_option_row(int x, int y, const BotVersionSummary& row, bool highlighted);
    bool ensure_window();
};

#endif // RPS_H
