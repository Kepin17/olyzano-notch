CREATE TABLE categories (
    id         TEXT PRIMARY KEY,
    name       TEXT NOT NULL UNIQUE COLLATE NOCASE,
    created_at TEXT NOT NULL
);
ALTER TABLE file_items ADD COLUMN category_id TEXT REFERENCES categories(id);
CREATE INDEX idx_file_items_category ON file_items(category_id);
