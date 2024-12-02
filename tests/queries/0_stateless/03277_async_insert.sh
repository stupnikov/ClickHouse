#!/usr/bin/env bash
# Tags: no-fasttest

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

set -e


mkdir -p "${CLICKHOUSE_USER_FILES_UNIQUE:?}"
rm -rf "${CLICKHOUSE_USER_FILES_UNIQUE:?}"/*


FILE_CSV="${CLICKHOUSE_USER_FILES_UNIQUE:?}/03277.csv"
FILE_ARROW="${CLICKHOUSE_USER_FILES_UNIQUE:?}/03277.arrow"


$CLICKHOUSE_CLIENT -q "
    SET async_insert = 1;
    SELECT '${FILE_CSV}';
    CREATE TABLE t0 (c0 Int) ENGINE = File(CSV);
    INSERT INTO TABLE t0 (c0) VALUES (1);
    INSERT INTO TABLE FUNCTION file('$FILE_CSV', 'CSV', 'c0 Int') SELECT c0 FROM t0;
    INSERT INTO TABLE t0 (c0) FROM INFILE '$FILE_CSV' FORMAT CSV;
"
$CLICKHOUSE_CLIENT -q "SELECT * from t0"
$CLICKHOUSE_CLIENT -q "DROP TABLE t0"


$CLICKHOUSE_CLIENT -q "
    SET async_insert = 1;
    SELECT '${FILE_ARROW}';
    CREATE TABLE t0 (c0 Int) ENGINE = Join(ANY, INNER, c0);
    INSERT INTO TABLE FUNCTION file('$FILE_ARROW', 'Arrow', 'c0 Int') SELECT c0 FROM t0;
    INSERT INTO TABLE t0 (c0) FROM INFILE '$FILE_ARROW' FORMAT Arrow;
"
$CLICKHOUSE_CLIENT -q "SELECT * from t0"
$CLICKHOUSE_CLIENT -q "DROP TABLE t0"


rm -rf "${CLICKHOUSE_USER_FILES_UNIQUE:?}"/*