BEGIN;

DROP TRIGGER IF EXISTS trg_bots_set_updated_at ON bots;
DROP FUNCTION IF EXISTS set_updated_at();

DROP TABLE IF EXISTS match_results;
DROP TABLE IF EXISTS match_entries;
DROP TABLE IF EXISTS matches;
DROP TABLE IF EXISTS bot_versions;
DROP TABLE IF EXISTS bots;
DROP TABLE IF EXISTS users;

DROP EXTENSION IF EXISTS pgcrypto;

COMMIT;
