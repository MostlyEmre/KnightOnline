#!/usr/bin/env bash
# Dumps the facts about the reference database that the SQLite port has to match:
# collation, procedures, per-table row counts and fixed-width / blob-ish columns.
set -euo pipefail

sql() {
	docker compose exec -T sqlserver /opt/mssql-tools18/bin/sqlcmd \
		-S localhost -U sa -P "${MSSQL_SA_PASSWORD}" -C -d KN_online -W -s '|' -Q "SET NOCOUNT ON; $1"
}

echo "== server version / database collation"
sql "SELECT @@VERSION AS version, DATABASEPROPERTYEX('KN_online', 'Collation') AS db_collation"

echo "== procedures"
sql "SELECT s.name AS [schema], p.name FROM sys.procedures p JOIN sys.schemas s ON s.schema_id = p.schema_id ORDER BY p.name"

echo "== views"
sql "SELECT s.name AS [schema], v.name FROM sys.views v JOIN sys.schemas s ON s.schema_id = v.schema_id ORDER BY v.name"

echo "== triggers"
sql "SELECT name, OBJECT_NAME(parent_id) AS parent FROM sys.triggers ORDER BY name"

echo "== table row counts"
sql "SELECT t.name, SUM(p.rows) AS row_count FROM sys.tables t JOIN sys.partitions p ON p.object_id = t.object_id AND p.index_id IN (0, 1) GROUP BY t.name ORDER BY t.name"

echo "== char/varchar/binary columns (type, length, collation)"
sql "SELECT t.name AS tbl, c.name AS col, ty.name AS type, c.max_length, c.is_nullable, c.collation_name FROM sys.columns c JOIN sys.tables t ON t.object_id = c.object_id JOIN sys.types ty ON ty.user_type_id = c.user_type_id WHERE ty.name IN ('char', 'nchar', 'varchar', 'nvarchar', 'binary', 'varbinary', 'text', 'image') ORDER BY t.name, c.column_id"

echo "== identity columns"
sql "SELECT OBJECT_NAME(object_id) AS tbl, name FROM sys.identity_columns ORDER BY 1"
