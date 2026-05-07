#include "rps_internal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;
namespace fs = std::filesystem;

// Platform-specific process helpers.
#ifdef _WIN32
static FILE* portable_popen(const char* command, const char* mode) {
    return _popen(command, mode);
}

static int portable_pclose(FILE* pipe) {
    return _pclose(pipe);
}
#else
static FILE* portable_popen(const char* command, const char* mode) {
    return popen(command, mode);
}

static int portable_pclose(FILE* pipe) {
    return pclose(pipe);
}
#endif

/**
 * Returns true when a character code is printable ASCII.
 */
static bool is_printable_ascii(int ch) {
    return ch >= 32 && ch <= 126;
}

/**
 * Returns a lowercase copy of the input text.
 */
static string lower_copy(string text) {
    transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/**
 * Checks whether a path has a .asm extension, case-insensitively.
 */
static bool has_asm_extension(const fs::path& path) {
    return lower_copy(path.extension().string()) == ".asm";
}

/**
 * Returns display text for a filesystem path.
 */
static string display_path(const fs::path& path) {
    return path.string();
}

/**
 * Lists directory entries with folders first, then name order.
 */
static vector<fs::directory_entry> list_directory_entries(const fs::path& dir) {
    vector<fs::directory_entry> entries;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        entries.push_back(entry);
    }

    sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        if (a.is_directory() != b.is_directory()) {
            return a.is_directory() > b.is_directory();
        }
        return lower_copy(a.path().filename().string()) < lower_copy(b.path().filename().string());
    });

    return entries;
}

/**
 * Formats a file-picker row label.
 */
static string browser_entry_label(const fs::directory_entry& entry) {
    string name = entry.path().filename().string();
    if (entry.is_directory()) {
        return "[D] " + name;
    }
    if (has_asm_extension(entry.path())) {
        return "[A] " + name;
    }
    return "[F] " + name;
}

/**
 * Reads an entire file into a byte vector.
 */
static bool read_binary_file(const fs::path& path, vector<unsigned char>& outBytes, string& error) {
    outBytes.clear();
    ifstream in(path, ios::binary);
    if (!in.is_open()) {
        error = "Failed to open file: " + path.string();
        return false;
    }

    outBytes.assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
    return true;
}

/**
 * Returns the directory of the running executable when available.
 */
static fs::path executable_directory() {
#ifdef _WIN32
    char buffer[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return fs::path(buffer).parent_path();
    }
#endif
    return fs::current_path();
}

/**
 * Finds a usable tnasm executable from known project and runtime locations.
 */
static fs::path locate_tnasm_executable() {
    const fs::path cwd = fs::current_path();
    const fs::path exe_dir = executable_directory();
    const fs::path candidates[] = {
        cwd / "tnasm.exe",
        cwd / "tnasm",
        cwd / "build" / "out" / "bin" / "tnasm.exe",
        cwd / "out" / "bin" / "tnasm.exe",
        exe_dir / "tnasm.exe",
        exe_dir / "build" / "out" / "bin" / "tnasm.exe",
        exe_dir / "out" / "bin" / "tnasm.exe"
    };

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}

/**
 * Compiles an assembly source file with tnasm and reads the produced .bin output.
 */
static bool compile_asm_with_tnasm(const fs::path& asm_path, vector<unsigned char>& compiled_bytes, string& compile_log, string& error) {
    compiled_bytes.clear();
    compile_log.clear();

    fs::path tnasm_exe = locate_tnasm_executable();
    if (tnasm_exe.empty()) {
        error = "Could not find the tnasm executable next to the game executable.";
        return false;
    }

    fs::path previous_dir;
    try {
        previous_dir = fs::current_path();
        if (!asm_path.parent_path().empty()) {
            fs::current_path(asm_path.parent_path());
        }
    } catch (const std::exception&) {
        error = "Failed to set working directory for tnasm.";
        return false;
    }

    string cmd;
#ifdef _WIN32
    cmd = "cmd /c \"\"" + tnasm_exe.string() + "\" \"" + asm_path.string() + "\" 2>&1\"";
#else
    cmd = "\"" + tnasm_exe.string() + "\" \"" + asm_path.string() + "\" 2>&1";
#endif

    FILE* pipe = portable_popen(cmd.c_str(), "r");
    if (!pipe) {
        std::error_code restore_ec;
        fs::current_path(previous_dir, restore_ec);
        error = "Failed to launch tnasm process";
        return false;
    }

    char buffer[512] = {0};
    while (fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
        compile_log += buffer;
    }

    int status = portable_pclose(pipe);
    std::error_code restore_ec;
    fs::current_path(previous_dir, restore_ec);
    (void)status;

    fs::path bin_path = asm_path;
    bin_path.replace_extension(".bin");
    if (!fs::exists(bin_path)) {
        error = compile_log.empty() ? "tnasm did not produce a .bin file." : compile_log;
        return false;
    }

    if (!read_binary_file(bin_path, compiled_bytes, error)) {
        return false;
    }

    return true;
}

/**
 * Writes byte vector into a binary file path.
 */
static bool write_binary_file(const fs::path& file_path, const vector<unsigned char>& bytes, string& error) {
    ofstream out(file_path, ios::binary | ios::trunc);
    if (!out.is_open()) {
        error = "Failed to open output file: " + file_path.string();
        return false;
    }

    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    if (!out.good()) {
        error = "Failed to write output file: " + file_path.string();
        return false;
    }

    return true;
}

/**
 * Fetches compiled binaries for the three selected bot versions.
 */
bool Game::fetch_selected_binaries(DatabaseClient& db_client,
                                   long long rock_id,
                                   long long paper_id,
                                   long long scissors_id,
                                   vector<unsigned char>& rock_bytes,
                                   vector<unsigned char>& paper_bytes,
                                   vector<unsigned char>& scissors_bytes,
                                   string& db_error) {
    if (!db_client.fetchCompiledBotBinary(rock_id, rock_bytes, db_error)) {
        show_message_screen("Load error", {"Failed to load rock bot version " + to_string(rock_id) + ":", db_error});
        return false;
    }

    if (!db_client.fetchCompiledBotBinary(paper_id, paper_bytes, db_error)) {
        show_message_screen("Load error", {"Failed to load paper bot version " + to_string(paper_id) + ":", db_error});
        return false;
    }

    if (!db_client.fetchCompiledBotBinary(scissors_id, scissors_bytes, db_error)) {
        show_message_screen("Load error", {"Failed to load scissors bot version " + to_string(scissors_id) + ":", db_error});
        return false;
    }

    return true;
}

/**
 * Runs the authenticated gameplay flow:
 * upload/menu -> bot selection -> match launch/restart.
 */
Game::AuthFlowResult Game::run_authenticated_flow(DatabaseClient& db_client,
                                                  long long user_id,
                                                  const string& username,
                                                  string& db_error) {
    bool skip_upload_screen = false;

    while (!tigrClosed(screen)) {
        if (!skip_upload_screen) {
            const UploadScreenResult upload_result = show_upload_assembly_screen(db_client, user_id, username);
            if (upload_result == UploadScreenResult::ExitApplication) {
                return AuthFlowResult::ExitApplication;
            }
            if (upload_result == UploadScreenResult::ReturnToLogin) {
                return AuthFlowResult::RestartFlow;
            }
        }
        skip_upload_screen = false;

        vector<BotVersionSummary> accessible;
        if (!db_client.listAccessibleBotVersions(user_id, accessible, db_error)) {
            show_message_screen("Bot list error", {db_error});
            return AuthFlowResult::RestartFlow;
        }

        if (accessible.empty()) {
            show_message_screen("No bots available", {"No compiled bot versions are visible for this account.", "Create or publish compiled bots first."});
            return AuthFlowResult::RestartFlow;
        }

        long long rock_id = 0;
        long long paper_id = 0;
        long long scissors_id = 0;
        if (!show_bot_selection_screen(db_client, user_id, username, accessible, rock_id, paper_id, scissors_id)) {
            while (!tigrClosed(screen) && tigrKeyHeld(screen, TK_ESCAPE)) {
                tigrUpdate(screen);
            }
            skip_upload_screen = false;
            continue;
        }

        while (!tigrClosed(screen)) {
            vector<unsigned char> rock_bytes;
            vector<unsigned char> paper_bytes;
            vector<unsigned char> scissors_bytes;
            if (!fetch_selected_binaries(db_client,
                                         rock_id,
                                         paper_id,
                                         scissors_id,
                                         rock_bytes,
                                         paper_bytes,
                                         scissors_bytes,
                                         db_error)) {
                break;
            }

            const auto now = std::chrono::system_clock::now().time_since_epoch();
            const auto stamp = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
            const fs::path runtime_dir = fs::temp_directory_path() / "teenyat_rps" / ("match_" + to_string(stamp));

            std::error_code ec;
            fs::create_directories(runtime_dir, ec);
            if (ec) {
                show_message_screen("Filesystem error", {"Failed to create runtime directory:", runtime_dir.string()});
                break;
            }

            const fs::path rock_path = runtime_dir / "rock.bin";
            const fs::path paper_path = runtime_dir / "paper.bin";
            const fs::path scissors_path = runtime_dir / "scissors.bin";

            if (!write_binary_file(rock_path, rock_bytes, db_error) ||
                !write_binary_file(paper_path, paper_bytes, db_error) ||
                !write_binary_file(scissors_path, scissors_bytes, db_error)) {
                show_message_screen("Filesystem error", {db_error});
                fs::remove_all(runtime_dir, ec);
                break;
            }

            if (!initialize(rock_path.string().c_str(), paper_path.string().c_str(), scissors_path.string().c_str())) {
                show_message_screen("Game error", {"Failed to initialize the match using the selected bot versions."});
                fs::remove_all(runtime_dir, ec);
                break;
            }

            if (!run()) {
                fs::remove_all(runtime_dir, ec);
                return AuthFlowResult::ExitApplication;
            }

            fs::remove_all(runtime_dir, ec);
            while (!tigrClosed(screen) && tigrKeyHeld(screen, TK_ESCAPE)) {
                tigrUpdate(screen);
            }
            skip_upload_screen = true;
            break;
        }
    }

    return AuthFlowResult::ExitApplication;
}

/**
 * Reloads file picker directory entries and resets selection state.
 */
static void refresh_browser_entries(const fs::path& current_dir,
                                    vector<fs::directory_entry>& browser_entries,
                                    size_t& selected_entry,
                                    string& error_message) {
    browser_entries = list_directory_entries(current_dir);
    selected_entry = 0;
    if (browser_entries.empty()) {
        error_message = "No files found in this folder.";
    } else {
        error_message.clear();
    }
}

/**
 * Navigates one level up in the file picker directory tree.
 */
static void go_up_directory(fs::path& current_dir,
                            vector<fs::directory_entry>& browser_entries,
                            size_t& selected_entry,
                            string& error_message) {
    fs::path parent = current_dir.parent_path();
    if (!parent.empty() && parent != current_dir) {
        current_dir = parent;
        refresh_browser_entries(current_dir, browser_entries, selected_entry, error_message);
    }
}

/**
 * Converts visibility picker index into database visibility string.
 */
static string visibility_from_selection(int selected_visibility) {
    if (selected_visibility == 0) {
        return "private";
    }
    if (selected_visibility == 1) {
        return "public";
    }
    return "unlisted";
}

/**
 * Marks a bot version compile result as failed with status details.
 */
static void mark_compile_failed(DatabaseClient& db_client,
                                long long created_version_id,
                                const string& status_message,
                                string& out_db_error) {
    db_client.setBotVersionCompilation(created_version_id, {}, status_message, false, out_db_error);
}

/**
 * Uploads an assembly file, compiles it with tnasm, and stores compile artifacts.
 */
static bool upload_assembly_file(DatabaseClient& db_client,
                                 long long userId,
                                 const string& username,
                                 const string& bot_name,
                                 const string& asm_path,
                                 int selected_visibility,
                                 const std::function<void(long long, const string&, const vector<string>&, const string&)>& show_failure_result,
                                 const std::function<void(long long, const string&, const vector<string>&, const string&)>& show_success_result,
                                 string& error_message) {
    if (bot_name.empty()) {
        error_message = "Bot name is required.";
        return false;
    }

    if (asm_path.empty()) {
        error_message = "Assembly file path is required.";
        return false;
    }

    const fs::path source_path(asm_path);
    if (!has_asm_extension(source_path)) {
        error_message = "Please choose a .asm file.";
        return false;
    }

    ifstream in(asm_path, ios::in | ios::binary);
    if (!in.is_open()) {
        error_message = "Failed to open file: " + asm_path;
        return false;
    }

    string source((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    if (source.empty()) {
        error_message = "Assembly file is empty.";
        return false;
    }

    const string visibility = visibility_from_selection(selected_visibility);
    long long created_version_id = 0;

    if (!db_client.createBotVersionFromAsm(userId, bot_name, visibility, source, created_version_id, error_message)) {
        show_failure_result(created_version_id, visibility, {"Database insert failed.", error_message}, error_message);
        return false;
    }

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto stamp = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    const fs::path temp_dir = fs::temp_directory_path() / "teenyat_rps" / "asm_uploads";

    std::error_code ec;
    fs::create_directories(temp_dir, ec);
    if (ec) {
        string db_error;
        mark_compile_failed(db_client, created_version_id, "Failed to create temp compile directory.", db_error);
        error_message = "Failed to create temporary compile directory.";
        show_failure_result(created_version_id, visibility, {error_message}, db_error.empty() ? error_message : db_error);
        return false;
    }

    const fs::path temp_asm = temp_dir / ("upload_" + to_string(userId) + "_" + to_string(created_version_id) + "_" + to_string(stamp) + ".asm");

    {
        ofstream out(temp_asm, ios::binary | ios::trunc);
        if (!out.is_open()) {
            string db_error;
            mark_compile_failed(db_client, created_version_id, "Failed to create temp asm file.", db_error);
            error_message = "Failed to create temporary asm file.";
            show_failure_result(created_version_id, visibility, {error_message}, db_error.empty() ? error_message : db_error);
            return false;
        }

        out.write(source.data(), static_cast<std::streamsize>(source.size()));
        if (!out.good()) {
            std::error_code rm_ec;
            fs::remove(temp_asm, rm_ec);

            string db_error;
            mark_compile_failed(db_client, created_version_id, "Failed to write temp asm file.", db_error);
            error_message = "Failed to write temporary asm file.";
            show_failure_result(created_version_id, visibility, {error_message}, db_error.empty() ? error_message : db_error);
            return false;
        }
    }

    vector<unsigned char> compiled_bytes;
    string compile_log;
    string compile_error;
    const bool compiled_ok = compile_asm_with_tnasm(temp_asm, compiled_bytes, compile_log, compile_error);

    std::error_code rm_ec;
    fs::remove(temp_asm, rm_ec);
    fs::path temp_bin = temp_asm;
    temp_bin.replace_extension(".bin");
    fs::remove(temp_bin, rm_ec);

    if (!compiled_ok) {
        string db_error;
        mark_compile_failed(db_client, created_version_id, compile_error, db_error);
        error_message = compile_error.empty() ? "Compilation failed." : compile_error;
        const string failure_log = compile_log.empty() ? error_message : compile_log;
        show_failure_result(created_version_id, visibility, {error_message}, failure_log);
        return false;
    }

    string db_error;
    if (!db_client.setBotVersionCompilation(created_version_id, compiled_bytes, compile_log, true, db_error)) {
        error_message = db_error;
        show_failure_result(created_version_id, visibility, {"Compilation succeeded, but storing the binary failed.", error_message}, compile_log);
        return false;
    }

    vector<string> success_lines = {
        "Uploaded bot source for user " + username + ".",
        "Created bot_version_id=" + to_string(created_version_id),
        "Source compiled and stored successfully."
    };
    show_success_result(created_version_id, visibility, success_lines, compile_log);
    error_message.clear();
    return true;
}

/**
 * Renders and handles the assembly upload UI screen.
 */
Game::UploadScreenResult Game::show_upload_assembly_screen(DatabaseClient& db_client, long long userId, const string& username) {
    if (!ensure_window()) {
        return UploadScreenResult::ExitApplication;
    }

    string bot_name;
    string asm_path;
    string error_message;
    fs::path current_dir = fs::current_path();
    vector<fs::directory_entry> browser_entries = list_directory_entries(current_dir);
    size_t selected_entry = 0;
    int focus_area = 0; // 0=continue, 1=bot name, 2=browser, 3=visibility, 4=upload
    int selected_visibility = 0; // 0=private, 1=public, 2=unlisted

    auto show_failure_result = [&](long long bot_version_id,
                                   const string& visibility,
                                   const vector<string>& lines,
                                   const string& log_text) {
        show_upload_result_screen(bot_name, bot_version_id, visibility, false, lines, log_text);
    };

    auto show_success_result = [&](long long bot_version_id,
                                   const string& visibility,
                                   const vector<string>& lines,
                                   const string& log_text) {
        show_upload_result_screen(bot_name, bot_version_id, visibility, true, lines, log_text);
    };

    while (!tigrClosed(screen)) {
        tigrFillRect(screen, 0, 0, GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE, UI_BACKGROUND_COLOR);

        draw_centered_text(screen, 24, UI_TEXT_COLOR, "Upload Assembly File");
        draw_centered_text(screen, 50, UI_TEXT_COLOR, "Logged in as " + username + ". Choose a bot name, browse for a .asm file, and set visibility.");
        draw_centered_text(screen, 72, UI_TEXT_COLOR, "Up/Down moves through sections, Left/Right controls the selected section, Enter activates.");
        draw_centered_text(screen, 90, UI_TEXT_COLOR, "For file selection: Backspace to go back a directory, Enter to go into one, Left/Right to tab through.");

        int continue_x = 250;
        int continue_y = 118;
        int continue_w = 140;
        int continue_h = 36;
        TPixel continue_fill = (focus_area == 0) ? tigrRGB(35, 70, 120) : tigrRGB(20, 20, 20);
        TPixel continue_border = (focus_area == 0) ? tigrRGB(90, 160, 255) : tigrRGB(120, 120, 120);
        tigrFillRect(screen, continue_x, continue_y, continue_w, continue_h, continue_fill);
        tigrRect(screen, continue_x, continue_y, continue_w, continue_h, continue_border);
        draw_centered_in_box(screen, continue_x, continue_y, continue_w, continue_h, UI_TEXT_COLOR, "Continue");

        draw_input_box(90, 170, 500, "Bot Name", bot_name, focus_area == 1, false);

        draw_text(screen, 90, 220, UI_TEXT_COLOR, "File Picker");
        draw_text(screen, 90, 240, UI_TEXT_COLOR, "Folder: " + display_path(current_dir));

        tigrRect(screen, 90, 260, 500, 280, focus_area == 2 ? tigrRGB(0, 90, 200) : tigrRGB(160, 160, 160));

        size_t visible_start = 0;
        const size_t visible_rows = 9;
        if (selected_entry >= visible_rows) {
            visible_start = selected_entry - visible_rows + 1;
        }

        int browser_y = 268;
        if (browser_entries.empty()) {
            draw_text(screen, 102, browser_y, tigrRGB(120, 120, 120), "(no files in this folder)");
        } else {
            for (size_t i = visible_start; i < browser_entries.size() && i < visible_start + visible_rows; ++i) {
                bool highlighted = (focus_area == 2 && i == selected_entry);
                TPixel row_fill = highlighted ? tigrRGB(35, 70, 120) : tigrRGB(20, 20, 20);
                TPixel row_border = highlighted ? tigrRGB(90, 160, 255) : tigrRGB(120, 120, 120);
                tigrFillRect(screen, 98, browser_y - 2, 484, 26, row_fill);
                tigrRect(screen, 98, browser_y - 2, 484, 26, row_border);
                draw_text(screen, 106, browser_y + 4, UI_TEXT_COLOR, browser_entry_label(browser_entries[i]));
                browser_y += 28;
            }
        }

        draw_text(screen, 90, 552, UI_TEXT_COLOR, "Selected file: " + (asm_path.empty() ? string("(none)") : asm_path));

        draw_text(screen, 90, 580, UI_TEXT_COLOR, "Visibility");

        const char* visibility_labels[3] = {"Private", "Public", "Unlisted"};
        int vis_x = 90;
        for (int i = 0; i < 3; ++i) {
            bool is_selected = (selected_visibility == i);
            TPixel fill = is_selected ? tigrRGB(35, 70, 120) : tigrRGB(20, 20, 20);
            TPixel border = (focus_area == 3 && is_selected) ? tigrRGB(90, 160, 255) : (is_selected ? tigrRGB(180, 220, 255) : tigrRGB(120, 120, 120));
            tigrFillRect(screen, vis_x, 605, 150, 34, fill);
            tigrRect(screen, vis_x, 605, 150, 34, border);
            draw_centered_in_box(screen, vis_x, 605, 150, 34, UI_TEXT_COLOR, visibility_labels[i]);
            vis_x += 160;
        }

        draw_text(screen, 90, 650, UI_TEXT_COLOR, "In browser: Left/Right changes selection, Enter opens/selects, Backspace goes up.");

        int upload_x = 250;
        int button_y = 690;
        int button_w = 140;
        int button_h = 36;

        TPixel upload_fill = (focus_area == 4) ? tigrRGB(35, 70, 120) : tigrRGB(20, 20, 20);
        TPixel upload_border = (focus_area == 4) ? tigrRGB(90, 160, 255) : tigrRGB(120, 120, 120);

        tigrFillRect(screen, upload_x, button_y, button_w, button_h, upload_fill);
        tigrRect(screen, upload_x, button_y, button_w, button_h, upload_border);
        draw_centered_in_box(screen, upload_x, button_y, button_w, button_h, UI_TEXT_COLOR, "Upload");

        if (!error_message.empty()) {
            draw_text(screen, 90, 750, UI_TEXT_COLOR, error_message);
        }

        tigrUpdate(screen);

        int ch = tigrReadChar(screen);

        if (tigrKeyDown(screen, TK_ESCAPE)) {
            return UploadScreenResult::ReturnToLogin;
        }

        if (tigrKeyDown(screen, TK_UP)) {
            focus_area = (focus_area + 4) % 5;
        }

        if (tigrKeyDown(screen, TK_DOWN)) {
            focus_area = (focus_area + 1) % 5;
        }

        if (focus_area == 2) {
            if (tigrKeyDown(screen, TK_LEFT) && selected_entry > 0) {
                selected_entry--;
            }
            if (tigrKeyDown(screen, TK_RIGHT) && (selected_entry + 1) < browser_entries.size()) {
                selected_entry++;
            }
            if (tigrKeyDown(screen, TK_BACKSPACE)) {
                go_up_directory(current_dir, browser_entries, selected_entry, error_message);
            }
        }

        if (focus_area == 3) {
            if (tigrKeyDown(screen, TK_LEFT)) {
                selected_visibility = (selected_visibility + 2) % 3;
            }
            if (tigrKeyDown(screen, TK_RIGHT)) {
                selected_visibility = (selected_visibility + 1) % 3;
            }
        }

        if (tigrKeyDown(screen, TK_BACKSPACE)) {
            if (focus_area == 1) {
                if (!bot_name.empty()) bot_name.pop_back();
            }
        }

        if (is_printable_ascii(ch)) {
            if (focus_area == 1) {
                bot_name.push_back(static_cast<char>(ch));
            }
        }

        if (tigrKeyDown(screen, TK_RETURN)) {
            if (focus_area == 0) {
                return UploadScreenResult::ContinueFlow;
            }
            if (focus_area == 2) {
                if (browser_entries.empty()) {
                    continue;
                }
                const auto& entry = browser_entries[selected_entry];
                if (entry.is_directory()) {
                    current_dir = entry.path();
                    refresh_browser_entries(current_dir, browser_entries, selected_entry, error_message);
                    continue;
                }
                if (has_asm_extension(entry.path())) {
                    asm_path = entry.path().string();
                    focus_area = 3;
                    continue;
                }
                error_message = "Choose a .asm file.";
                continue;
            }

            if (focus_area == 3) {
                continue;
            }

            if (focus_area == 4) {
                upload_assembly_file(db_client,
                                     userId,
                                     username,
                                     bot_name,
                                     asm_path,
                                     selected_visibility,
                                     show_failure_result,
                                     show_success_result,
                                     error_message);
                continue;
            }
        }

        if (focus_area == 2) {
            if (tigrKeyDown(screen, TK_PAGEUP)) {
                go_up_directory(current_dir, browser_entries, selected_entry, error_message);
            }
        }
    }

    return UploadScreenResult::ExitApplication;
}

/**
 * Entry point for database-backed mode:
 * connection -> auth -> authenticated session flow.
 */
bool Game::run_database_mode() {
    if (!ensure_window()) {
        return false;
    }

    while (!tigrClosed(screen)) {
        DatabaseConfig db_config = DatabaseConfig::fromEnvironment();

        DatabaseClient db_client(db_config);
        string db_error;

        while (!tigrClosed(screen)) {
            if (!db_client.connect(db_error)) {
                return false;
            }

            long long user_id = 0;
            string username;
            bool create_account_requested = false;

            while (!tigrClosed(screen)) {
                const bool login_ok = show_login_screen(db_client, user_id, username, create_account_requested);
                if (!login_ok) {
                    if (!create_account_requested) {
                        return false;
                    }

                    const bool account_created = show_create_account_screen(db_client, user_id, username);
                    create_account_requested = false;
                    if (!account_created) {
                        continue;
                    }
                } else {
                    create_account_requested = false;
                }

                const AuthFlowResult session_result = run_authenticated_flow(db_client,
                                                                             user_id,
                                                                             username,
                                                                             db_error);

                if (session_result == AuthFlowResult::ExitApplication) {
                    return false;
                }

                if (session_result == AuthFlowResult::MatchCompleted) {
                    return true;
                }

                if (session_result == AuthFlowResult::RestartFlow) {
                    continue;
                }

                break;
            }
        }
    }

    return false;
}
