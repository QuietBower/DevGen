import os
import sqlite3
import json
import sys
from pathlib import Path

def create_table_and_import(conn, table_name, jsonl_path):
    cur = conn.cursor()

    # 1. Create the table (use the identifier safely)
    # Note: SQLite table names cannot be parameterized, so escape them manually (only letters, digits and underscores allowed)
    if not table_name.replace("_", "").replace("-", "").isalnum():
        raise ValueError(f"Invalid table name: {table_name}")
    
    cur.execute(f'''
        CREATE TABLE IF NOT EXISTS "{table_name}" (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            filename TEXT NOT NULL,
            source TEXT NOT NULL
        )
    ''')

    # 2. Create an index (to speed up lookups by name)
    cur.execute(f'CREATE INDEX IF NOT EXISTS "idx_{table_name}_name" ON "{table_name}"(name);')

    # 3. Stream-read and batch-insert
    batch = []
    BATCH_SIZE = 10000

    cnt = 0
    with open(jsonl_path, 'r', encoding='utf-8') as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                name = obj.get('name', obj.get('alias', ''))
                filename = obj.get('filename', '')
                source = obj.get('source', '')
                batch.append((name, filename, source))
            except json.JSONDecodeError as e:
                print(f"⚠️  {jsonl_path}:{line_num} JSON 解析失败: {e}", file=sys.stderr)
                continue

            if len(batch) >= BATCH_SIZE:
                cnt += len(batch)
                cur.executemany(
                    f'INSERT INTO "{table_name}" (name, filename, source) VALUES (?, ?, ?)',
                    batch
                )
                conn.commit()
                batch.clear()

        # Insert the remaining data
        if batch:
            cnt += len(batch)
            cur.executemany(
                f'INSERT INTO "{table_name}" (name, filename, source) VALUES (?, ?, ?)',
                batch
            )
            conn.commit()

    print(f"✅ 已导入 {jsonl_path} → 表 '{table_name}', 共 {cnt} 条数据")

def main(data_dir, db_path):
    data_dir = Path(data_dir)
    if not data_dir.is_dir():
        raise FileNotFoundError(f"目录不存在: {data_dir}")

    conn = sqlite3.connect(db_path)
    # Performance tuning
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA synchronous=NORMAL;")
    conn.execute("PRAGMA cache_size=100000;")  # ~100MB

    for jsonl_file in data_dir.glob("*.jsonl"):
        table_name = jsonl_file.stem  # Strip the .jsonl suffix
        create_table_and_import(conn, table_name, jsonl_file)

    conn.close()
    print(f"\n🎉 所有文件已导入到 {db_path}")

if __name__ == "__main__":
    main("/home/user/data/driver_info/jsonl", "driver_info.db")