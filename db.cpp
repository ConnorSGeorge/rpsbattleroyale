#include "db.h"

#include <cstdlib>
#include <sstream>
#include <cstdio>
#include <array>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <utility>

using namespace std;
namespace fs = std::filesystem;

// Platform-specific process and environment helpers.
#ifdef _WIN32
static int portable_putenv(const string& key, const string& value) {
    return _putenv_s(key.c_str(), value.c_str());
}

static FILE* portable_popen(const char* command, const char* mode) {
    return _popen(command, mode);
}

static int portable_pclose(FILE* pipe) {
    return _pclose(pipe);
}
#else
static int portable_putenv(const string& key, const string& value) {
    if (value.empty()) {
        return unsetenv(key.c_str());
    }
    return setenv(key.c_str(), value.c_str(), 1);
}

static FILE* portable_popen(const char* command, const char* mode) {
    return popen(command, mode);
}

static int portable_pclose(FILE* pipe) {
    return pclose(pipe);
}
#endif

/**
 * Returns a trimmed copy of input text (leading/trailing whitespace removed).
 */
static string trim_copy(const string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        start++;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }

    return value.substr(start, end - start);
}

/**
 * Reads an environment variable and returns a trimmed value, or an empty string.
 */
static string env_or_empty(const char* key) {
    const char* value = std::getenv(key);
    return value ? trim_copy(string(value)) : string();
}

/**
 * Builds DatabaseConfig from environment variables and project defaults.
 */
DatabaseConfig DatabaseConfig::fromEnvironment() {
    DatabaseConfig config;
    config.host = env_or_empty("DB_HOST");
    config.port = env_or_empty("DB_PORT");
    config.dbname = env_or_empty("DB_NAME");
    config.user = env_or_empty("DB_USER");
    config.password = env_or_empty("DB_PASS");

    return config;
}

/**
 * Checks whether all required database connection fields are present.
 */
bool DatabaseConfig::isComplete() const {
    return !trim_copy(host).empty() &&
           !trim_copy(port).empty() &&
           !trim_copy(dbname).empty() &&
           !trim_copy(user).empty() &&
           !trim_copy(password).empty();
}

/**
 * Returns a comma-separated list of missing environment-backed fields.
 */
string DatabaseConfig::missingFields() const {
    vector<string> missing;
    if (trim_copy(host).empty()) missing.push_back("DB_HOST");
    if (trim_copy(port).empty()) missing.push_back("DB_PORT");
    if (trim_copy(dbname).empty()) missing.push_back("DB_NAME");
    if (trim_copy(user).empty()) missing.push_back("DB_USER");
    if (trim_copy(password).empty()) missing.push_back("DB_PASS");

    if (missing.empty()) {
        return {};
    }

    string joined;
    for (size_t i = 0; i < missing.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += missing[i];
    }
    return joined;
}

/**
 * Returns a connection summary with password masked for logging/debugging.
 */
string DatabaseConfig::maskedConnectionSummary() const {
    return "host=" + host + " port=" + port + " dbname=" + dbname + " user=" + user + " password=******";
}

DatabaseClient::DatabaseClient(DatabaseConfig config)
    : config_(std::move(config)), connected_(false) {}

/**
 * Verifies database connectivity with a lightweight test query.
 */
bool DatabaseClient::connect(string& error) {
    string output;
    if (!runPsqlQuery("SELECT 1;", output, error)) {
        connected_ = false;
        return false;
    }

    connected_ = true;
    return true;
}

bool DatabaseClient::isConnected() const {
    return connected_;
}

/**
 * Escapes single quotes for safe SQL string literal insertion.
 */
static string sql_escape_literal(const string& input) {
    string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '\'') {
            out += "''";
        } else {
            out += c;
        }
    }
    return out;
}

/**
 * Splits text by newline, removing empty lines and trailing CR characters.
 */
static vector<string> split_lines(const string& text) {
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

/**
 * Splits a single row of tab-delimited output into columns.
 */
static vector<string> split_tab(const string& text) {
    vector<string> parts;
    string current;
    for (char c : text) {
        if (c == '\t') {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

/**
 * Returns lowercase copy of input text.
 */
static string to_lower_copy(string text) {
    transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/**
 * Encodes binary bytes into lowercase hex string.
 */
static string bytes_to_hex(const vector<unsigned char>& bytes) {
    static const char* digits = "0123456789abcdef";
    string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        out.push_back(digits[(byte >> 4) & 0x0F]);
        out.push_back(digits[byte & 0x0F]);
    }
    return out;
}

/**
 * Creates a new user account and returns the generated user id.
 */
bool DatabaseClient::createUser(const string& username, const string& password, long long& outUserId, string& error) const {
    outUserId = 0;

    if (!connected_) {
        error = "Database client is not connected";
        return false;
    }

    if (username.empty() || password.empty()) {
        error = "Username and password are required";
        return false;
    }

    string escaped_username = sql_escape_literal(username);
    string escaped_password = sql_escape_literal(password);

    string sql =
        "INSERT INTO users (username, password_hash) "
        "VALUES ('" + escaped_username + "', crypt('" + escaped_password + "', gen_salt('bf'))) "
        "RETURNING id;";

    string output;
    if (!runPsqlQuery(sql, output, error)) {
        string lowered = to_lower_copy(error);
        if (lowered.find("unique") != string::npos || lowered.find("duplicate") != string::npos) {
            error = "That username is already taken.";
        }
        return false;
    }

    vector<string> lines = split_lines(output);
    if (lines.empty()) {
        error = "Unexpected empty response while creating user";
        return false;
    }

    char* end_ptr = nullptr;
    long long parsed = std::strtoll(lines[0].c_str(), &end_ptr, 10);
    if (end_ptr == lines[0].c_str() || (end_ptr && *end_ptr != '\0') || parsed <= 0) {
        error = "Unexpected user id returned from database";
        return false;
    }

    outUserId = parsed;
    return true;
}

/**
 * Authenticates username/password and returns matching user id.
 */
bool DatabaseClient::authenticateUser(const string& username, const string& password, long long& outUserId, string& error) const {
    outUserId = 0;

    if (!connected_) {
        error = "Database client is not connected";
        return false;
    }

    string escaped_username = sql_escape_literal(username);
    string escaped_password = sql_escape_literal(password);

    string sql =
        "SELECT id FROM users "
        "WHERE username = '" + escaped_username + "' "
        "AND password_hash = crypt('" + escaped_password + "', password_hash) "
        "LIMIT 1;";

    string output;
    if (!runPsqlQuery(sql, output, error)) {
        return false;
    }

    vector<string> lines = split_lines(output);
    if (lines.empty()) {
        error = "Invalid username or password";
        return false;
    }

    char* end_ptr = nullptr;
    long long parsed = std::strtoll(lines[0].c_str(), &end_ptr, 10);
    if (end_ptr == lines[0].c_str() || (end_ptr && *end_ptr != '\0') || parsed <= 0) {
        error = "Unexpected user id returned from database";
        return false;
    }

    outUserId = parsed;
    return true;
}

/**
 * Creates a bot and pending bot_version row from uploaded assembly source.
 */
bool DatabaseClient::createBotVersionFromAsm(long long userId, const string& botName, const string& visibility, const string& sourceAsm, long long& outBotVersionId, string& error) const {
    outBotVersionId = 0;

    if (!connected_) {
        error = "Database client is not connected";
        return false;
    }

    if (userId <= 0) {
        error = "Invalid user id";
        return false;
    }

    string trimmed_bot_name = trim_copy(botName);
    if (trimmed_bot_name.empty()) {
        error = "Bot name is required";
        return false;
    }

    string normalized_visibility = to_lower_copy(trim_copy(visibility));
    if (normalized_visibility != "private" && normalized_visibility != "public" && normalized_visibility != "unlisted") {
        error = "Visibility must be private, public, or unlisted";
        return false;
    }

    if (sourceAsm.empty()) {
        error = "Assembly source is empty";
        return false;
    }

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto stamp = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    fs::path temp_dir = fs::temp_directory_path() / "teenyat_rps" / "uploads";
    std::error_code ec;
    fs::create_directories(temp_dir, ec);
    if (ec) {
        error = "Failed to create temporary upload directory";
        return false;
    }

    fs::path sql_path = temp_dir / ("upload_" + to_string(userId) + "_" + to_string(stamp) + ".sql");

    string tag = "rps_src_" + to_string(userId) + "_" + to_string(stamp);
    while (sourceAsm.find("$" + tag + "$") != string::npos) {
        tag += "x";
    }

    string escaped_name = sql_escape_literal(trimmed_bot_name);
    string escaped_visibility = sql_escape_literal(normalized_visibility);

    {
        ofstream out(sql_path, ios::binary | ios::trunc);
        if (!out.is_open()) {
            error = "Failed to create temporary SQL file";
            return false;
        }

        out << "BEGIN;\n";
        out << "WITH new_bot AS (\n";
        out << "    INSERT INTO bots (user_id, name, visibility)\n";
        out << "    VALUES (" << userId << ", '" << escaped_name << "', '" << escaped_visibility << "')\n";
        out << "    RETURNING id\n";
        out << "), new_version AS (\n";
        out << "    INSERT INTO bot_versions (bot_id, source_asm, source_hash, compile_status)\n";
        out << "    SELECT id, $" << tag << "$" << sourceAsm << "$" << tag << "$, encode(digest($" << tag << "$" << sourceAsm << "$" << tag << "$, 'sha256'), 'hex'), 'pending'\n";
        out << "    FROM new_bot\n";
        out << "    RETURNING id\n";
        out << ")\n";
        out << "SELECT id FROM new_version LIMIT 1;\n";
        out << "COMMIT;\n";

        if (!out.good()) {
            error = "Failed to write temporary SQL file";
            fs::remove(sql_path, ec);
            return false;
        }
    }

    string output;
    string previous_password = env_or_empty("PGPASSWORD");
    portable_putenv("PGPASSWORD", config_.password);

    string psql_cmd =
        "psql -h \"" + config_.host + "\" -p \"" + config_.port + "\" -U \"" + config_.user +
        "\" -d \"" + config_.dbname + "\" -v ON_ERROR_STOP=1 -f \"" + sql_path.string() + "\" 2>&1";

    FILE* pipe = portable_popen(psql_cmd.c_str(), "r");
    if (!pipe) {
        portable_putenv("PGPASSWORD", previous_password);
        fs::remove(sql_path, ec);
        error = "Failed to launch psql process";
        return false;
    }

    array<char, 512> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    int status = portable_pclose(pipe);
    portable_putenv("PGPASSWORD", previous_password);
    fs::remove(sql_path, ec);

    if (status != 0) {
        error = output.empty() ? "psql query failed" : output;
        return false;
    }

    vector<string> lines = split_lines(output);
    if (lines.empty()) {
        error = "Unexpected empty response while uploading assembly";
        return false;
    }

    long long parsed = 0;
    bool found_numeric_line = false;
    for (const auto& line : lines) {
        char* end_ptr = nullptr;
        long long candidate = std::strtoll(line.c_str(), &end_ptr, 10);
        if (end_ptr != line.c_str() && (!end_ptr || *end_ptr == '\0') && candidate > 0) {
            parsed = candidate;
            found_numeric_line = true;
        }
    }

    if (!found_numeric_line || parsed <= 0) {
        error = "Unexpected bot version id returned from database";
        return false;
    }

    outBotVersionId = parsed;
    return true;
}

/**
 * Stores compilation output (binary/log/status) for a bot version.
 */
bool DatabaseClient::setBotVersionCompilation(long long botVersionId, const vector<unsigned char>& compiledBytes, const string& compileLog, bool success, string& error) const {
    if (!connected_) {
        error = "Database client is not connected";
        return false;
    }

    if (botVersionId <= 0) {
        error = "Invalid bot version id";
        return false;
    }

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto stamp = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    fs::path temp_dir = fs::temp_directory_path() / "teenyat_rps" / "compiles";
    std::error_code ec;
    fs::create_directories(temp_dir, ec);
    if (ec) {
        error = "Failed to create temporary compile directory";
        return false;
    }

    fs::path sql_path = temp_dir / ("update_" + to_string(botVersionId) + "_" + to_string(stamp) + ".sql");

    string log_tag = "rps_log_" + to_string(botVersionId) + "_" + to_string(stamp);
    while (compileLog.find("$" + log_tag + "$") != string::npos) {
        log_tag += "x";
    }

    string hex_bytes = bytes_to_hex(compiledBytes);
    {
        ofstream out(sql_path, ios::binary | ios::trunc);
        if (!out.is_open()) {
            error = "Failed to create temporary SQL file";
            return false;
        }

        out << "BEGIN;\n";
        if (success) {
            out << "UPDATE bot_versions SET\n";
            out << "    compiled_bin = decode('" << hex_bytes << "', 'hex'),\n";
            out << "    compile_status = 'success',\n";
            out << "    compile_log = ";
            if (compileLog.empty()) {
                out << "NULL";
            } else {
                out << "$" << log_tag << "$" << compileLog << "$" << log_tag << "$";
            }
            out << "\n";
            out << "WHERE id = " << botVersionId << ";\n";
        } else {
            out << "UPDATE bot_versions SET\n";
            out << "    compiled_bin = NULL,\n";
            out << "    compile_status = 'failed',\n";
            out << "    compile_log = ";
            if (compileLog.empty()) {
                out << "NULL";
            } else {
                out << "$" << log_tag << "$" << compileLog << "$" << log_tag << "$";
            }
            out << "\n";
            out << "WHERE id = " << botVersionId << ";\n";
        }
        out << "COMMIT;\n";

        if (!out.good()) {
            error = "Failed to write temporary SQL file";
            fs::remove(sql_path, ec);
            return false;
        }
    }

    string output;
    string previous_password = env_or_empty("PGPASSWORD");
    portable_putenv("PGPASSWORD", config_.password);

    string psql_cmd =
        "psql -h \"" + config_.host + "\" -p \"" + config_.port + "\" -U \"" + config_.user +
        "\" -d \"" + config_.dbname + "\" -v ON_ERROR_STOP=1 -f \"" + sql_path.string() + "\" 2>&1";

    FILE* pipe = portable_popen(psql_cmd.c_str(), "r");
    if (!pipe) {
        portable_putenv("PGPASSWORD", previous_password);
        fs::remove(sql_path, ec);
        error = "Failed to launch psql process";
        return false;
    }

    array<char, 512> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    int status = portable_pclose(pipe);
    portable_putenv("PGPASSWORD", previous_password);
    fs::remove(sql_path, ec);

    if (status != 0) {
        error = output.empty() ? "psql update failed" : output;
        return false;
    }

    return true;
}

/**
 * Lists compiled bot versions visible to a user (owned + shared).
 */
bool DatabaseClient::listAccessibleBotVersions(long long userId, vector<BotVersionSummary>& outRows, string& error) const {
    outRows.clear();

    if (!connected_) {
        error = "Database client is not connected";
        return false;
    }

    string sql =
        "SELECT bv.id, u.id, u.username, b.name, CASE WHEN u.id = " + to_string(userId) + " THEN 1 ELSE 0 END "
        "FROM bot_versions bv "
        "JOIN bots b ON b.id = bv.bot_id "
        "JOIN users u ON u.id = b.user_id "
        "WHERE bv.compile_status = 'success' "
        "AND bv.compiled_bin IS NOT NULL "
        "AND (b.user_id = " + to_string(userId) + " OR b.visibility IN ('public', 'unlisted')) "
        "ORDER BY (u.id = " + to_string(userId) + ") DESC, u.username ASC, b.name ASC, bv.id DESC "
        "LIMIT 500;";

    string output;
    if (!runPsqlQuery(sql, output, error)) {
        return false;
    }

    vector<string> lines = split_lines(output);
    for (const string& line : lines) {
        vector<string> cols = split_tab(line);
        if (cols.size() != 5) {
            continue;
        }

        char* version_end_ptr = nullptr;
        char* owner_end_ptr = nullptr;
        long long version_id = std::strtoll(cols[0].c_str(), &version_end_ptr, 10);
        long long owner_id = std::strtoll(cols[1].c_str(), &owner_end_ptr, 10);
        if (version_end_ptr == cols[0].c_str() || (version_end_ptr && *version_end_ptr != '\0') || version_id <= 0) {
            continue;
        }
        if (owner_end_ptr == cols[1].c_str() || (owner_end_ptr && *owner_end_ptr != '\0') || owner_id <= 0) {
            continue;
        }

        BotVersionSummary row{};
        row.botVersionId = version_id;
        row.ownerUserId = owner_id;
        row.ownerUsername = cols[2];
        row.botName = cols[3];
        row.isOwner = (cols[4] == "1");
        outRows.push_back(std::move(row));
    }

    return true;
}

/**
 * Converts one hex character into its integer nibble value.
 */
static int hex_to_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/**
 * Fetches compiled bot binary bytes for a successful bot_version id.
 */
bool DatabaseClient::fetchCompiledBotBinary(long long botVersionId, vector<unsigned char>& outBytes, string& error) const {
    outBytes.clear();

    if (!connected_) {
        error = "Database client is not connected";
        return false;
    }

    string sql =
        "SELECT encode(compiled_bin, 'hex') "
        "FROM bot_versions "
        "WHERE id = " + to_string(botVersionId) + " "
        "AND compile_status = 'success' "
        "AND compiled_bin IS NOT NULL "
        "LIMIT 1;";

    string output;
    if (!runPsqlQuery(sql, output, error)) {
        return false;
    }

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' ' || output.back() == '\t')) {
        output.pop_back();
    }

    if (output.empty()) {
        error = "No compiled binary found for bot_version_id=" + to_string(botVersionId) + " (must exist with compile_status='success')";
        return false;
    }

    if ((output.size() % 2) != 0) {
        error = "Invalid hex output from database for bot_version_id=" + to_string(botVersionId);
        return false;
    }

    outBytes.reserve(output.size() / 2);
    for (size_t i = 0; i < output.size(); i += 2) {
        int high = hex_to_value(output[i]);
        int low = hex_to_value(output[i + 1]);
        if (high < 0 || low < 0) {
            error = "Invalid hex character in compiled binary for bot_version_id=" + to_string(botVersionId);
            return false;
        }
        outBytes.push_back(static_cast<unsigned char>((high << 4) | low));
    }

    return true;
}

/**
 * Executes a SQL command through psql and returns raw output/error text.
 */
bool DatabaseClient::runPsqlQuery(const string& sql, string& output, string& error) const {
    output.clear();
    error.clear();

    string previous_password = env_or_empty("PGPASSWORD");
    portable_putenv("PGPASSWORD", config_.password);

    string psql_cmd =
        "psql -h \"" + config_.host + "\" -p \"" + config_.port + "\" -U \"" + config_.user +
        "\" -d \"" + config_.dbname + "\" -At -F \"\t\" -v ON_ERROR_STOP=1 -c \"" + sql + "\" 2>&1";

    FILE* pipe = portable_popen(psql_cmd.c_str(), "r");
    if (!pipe) {
        portable_putenv("PGPASSWORD", previous_password);
        error = "Failed to launch psql process";
        return false;
    }

    array<char, 512> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    int status = portable_pclose(pipe);
    portable_putenv("PGPASSWORD", previous_password);

    if (status != 0) {
        if (output.empty()) {
            error = "psql query failed";
        } else {
            error = output;
        }
        return false;
    }

    return true;
}

/**
 * Marks the client as disconnected.
 */
void DatabaseClient::disconnect() {
    connected_ = false;
}
