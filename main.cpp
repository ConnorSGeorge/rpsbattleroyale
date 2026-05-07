#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "rps.h"

namespace fs = std::filesystem;

// .env parsing helpers.
static std::string trim_copy(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string::npos) {
        return {};
    }

    const std::size_t last = text.find_last_not_of(" \t\r\n\f\v");
    return text.substr(first, last - first + 1);
}

static std::string unquote_value(std::string value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
    }
    return value;
}

static bool set_env_var(const std::string& key, const std::string& value) {
#ifdef _WIN32
    return _putenv_s(key.c_str(), value.c_str()) == 0;
#else
    return setenv(key.c_str(), value.c_str(), 1) == 0;
#endif
}

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

// Searches the current directory and nearby executable directories for a .env file.
static fs::path find_dotenv_path() {
    const fs::path current = fs::current_path() / ".env";
    {
        std::error_code ec;
        if (fs::exists(current, ec) && !ec) {
            return current;
        }
    }

    const fs::path exe_dir = executable_directory();
    fs::path parent = exe_dir;
    for (int depth = 0; depth < 3; ++depth) {
        parent = parent.parent_path();
        if (parent.empty()) {
            break;
        }

        const fs::path candidate = parent / ".env";
        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec) {
            return candidate;
        }
    }

    return {};
}

static void load_dotenv_if_present() {
    const fs::path dotenv_path = find_dotenv_path();
    if (dotenv_path.empty()) {
        return;
    }

    std::ifstream in(dotenv_path);
    if (!in.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const std::size_t eq_pos = trimmed.find('=');
        if (eq_pos == std::string::npos || eq_pos == 0) {
            continue;
        }

        std::string key = trim_copy(trimmed.substr(0, eq_pos));
        std::string value = trim_copy(trimmed.substr(eq_pos + 1));
        value = unquote_value(value);

        if (!key.empty()) {
            (void)set_env_var(key, value);
        }
    }
}

int main() {
    // Load environment overrides before creating the game.
    load_dotenv_if_present();

    Game game;
    if (!game.run_database_mode()) {
        return 1;
    }

    return 0;
}
