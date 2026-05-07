BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE users (
    id BIGSERIAL PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT users_username_not_blank CHECK (length(trim(username)) > 0),
    CONSTRAINT users_password_hash_not_blank CHECK (length(trim(password_hash)) > 0)
);

CREATE TABLE bots (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    visibility TEXT NOT NULL DEFAULT 'private',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT bots_name_not_blank CHECK (length(trim(name)) > 0),
    CONSTRAINT bots_visibility_valid CHECK (visibility IN ('private', 'public', 'unlisted'))
);

CREATE TABLE bot_versions (
    id BIGSERIAL PRIMARY KEY,
    bot_id BIGINT NOT NULL REFERENCES bots(id) ON DELETE CASCADE,
    source_asm TEXT NOT NULL,
    source_hash CHAR(64) NOT NULL,
    compiled_bin BYTEA NULL,
    compile_status TEXT NOT NULL DEFAULT 'pending',
    compile_log TEXT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT bot_versions_status_valid CHECK (compile_status IN ('pending', 'success', 'failed')),
    CONSTRAINT bot_versions_source_not_blank CHECK (length(source_asm) > 0),
    CONSTRAINT bot_versions_source_hash_hex CHECK (source_hash ~ '^[0-9a-f]{64}$')
);

CREATE TABLE matches (
    id BIGSERIAL PRIMARY KEY,
    created_by_user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    status TEXT NOT NULL DEFAULT 'queued',
    seed BIGINT NULL,
    started_at TIMESTAMPTZ NULL,
    finished_at TIMESTAMPTZ NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT matches_status_valid CHECK (status IN ('queued', 'running', 'finished', 'failed', 'cancelled')),
    CONSTRAINT matches_time_order CHECK (finished_at IS NULL OR started_at IS NULL OR finished_at >= started_at)
);

CREATE TABLE match_entries (
    id BIGSERIAL PRIMARY KEY,
    match_id BIGINT NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    bot_version_id BIGINT NOT NULL REFERENCES bot_versions(id) ON DELETE RESTRICT,
    team_slot SMALLINT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT match_entries_team_slot_non_negative CHECK (team_slot >= 0),
    CONSTRAINT match_entries_unique_slot_per_match UNIQUE (match_id, team_slot)
);

CREATE TABLE match_results (
    id BIGSERIAL PRIMARY KEY,
    match_id BIGINT NOT NULL UNIQUE REFERENCES matches(id) ON DELETE CASCADE,
    winner_team SMALLINT NULL,
    stats_json JSONB NOT NULL DEFAULT '{}'::jsonb,
    replay_path TEXT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT match_results_winner_team_non_negative CHECK (winner_team IS NULL OR winner_team >= 0)
);

CREATE INDEX idx_bots_user_id ON bots(user_id);
CREATE INDEX idx_bot_versions_bot_id_created_at ON bot_versions(bot_id, created_at DESC);
CREATE INDEX idx_matches_created_by_user_id ON matches(created_by_user_id);
CREATE INDEX idx_matches_status_created_at ON matches(status, created_at DESC);
CREATE INDEX idx_match_entries_match_id ON match_entries(match_id);
CREATE INDEX idx_match_entries_user_id ON match_entries(user_id);
CREATE INDEX idx_match_results_match_id ON match_results(match_id);
CREATE INDEX idx_match_results_stats_json_gin ON match_results USING GIN (stats_json);

CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_bots_set_updated_at
BEFORE UPDATE ON bots
FOR EACH ROW
EXECUTE FUNCTION set_updated_at();

COMMIT;
