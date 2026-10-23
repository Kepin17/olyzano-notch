CREATE TABLE file_items (
    id            TEXT PRIMARY KEY,
    original_name TEXT NOT NULL,
    stored_name   TEXT NOT NULL UNIQUE,
    mime_type     TEXT,
    size_bytes    INTEGER NOT NULL,
    source_path   TEXT,
    added_at      TEXT NOT NULL,
    status        TEXT NOT NULL CHECK (status IN ('copying', 'available', 'error', 'pending_cleanup')),
    checksum      TEXT
);
CREATE INDEX idx_file_items_added_at ON file_items(added_at);
CREATE INDEX idx_file_items_status ON file_items(status);
