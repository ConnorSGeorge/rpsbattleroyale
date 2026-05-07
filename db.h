#ifndef DB_H
#define DB_H

#include <string>
#include <vector>

/**
 * Database connection settings loaded from environment variables.
 */
struct DatabaseConfig {
    std::string host;
    std::string port;
    std::string dbname;
    std::string user;
    std::string password;

    static DatabaseConfig fromEnvironment();
    bool isComplete() const;
    std::string missingFields() const;
    std::string maskedConnectionSummary() const;
};

/**
 * Compact summary of a visible bot version.
 */
struct BotVersionSummary {
    long long botVersionId;
    long long ownerUserId;
    std::string ownerUsername;
    std::string botName;
    bool isOwner;
};

/**
 * Database wrapper used by the game UI and upload flow.
 */
class DatabaseClient {
public:
    explicit DatabaseClient(DatabaseConfig config);

    bool connect(std::string& error);
    bool isConnected() const;
    bool createUser(const std::string& username, const std::string& password, long long& outUserId, std::string& error) const;
    bool authenticateUser(const std::string& username, const std::string& password, long long& outUserId, std::string& error) const;
    bool createBotVersionFromAsm(long long userId, const std::string& botName, const std::string& visibility, const std::string& sourceAsm, long long& outBotVersionId, std::string& error) const;
    bool setBotVersionCompilation(long long botVersionId, const std::vector<unsigned char>& compiledBytes, const std::string& compileLog, bool success, std::string& error) const;
    bool listAccessibleBotVersions(long long userId, std::vector<BotVersionSummary>& outRows, std::string& error) const;
    bool fetchCompiledBotBinary(long long botVersionId, std::vector<unsigned char>& outBytes, std::string& error) const;
    void disconnect();

private:
    bool runPsqlQuery(const std::string& sql, std::string& output, std::string& error) const;

    DatabaseConfig config_;
    bool connected_;
};

#endif // DB_H
