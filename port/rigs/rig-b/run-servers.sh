#!/usr/bin/env bash
# Starts the five servers headless in upstream's documented order, lets them run,
# records what they listen on and whether each stayed up, then stops them.
# Portable to macOS (bash 3.2, no ss) and Linux.
#
# Usage: run-servers.sh <bin dir> [seconds to keep running]
# For the SQLite backend set OPENKO_SQLITE_DB to the database file first.
set -uo pipefail

BIN_DIR="$1"
RUN_SECONDS="${2:-30}"
cd "$BIN_DIR"
mkdir -p run-logs

NAMES=(AIServer Ebenezer Aujard VersionManager ItemManager)
DELAYS=(20 20 10 5 5)
PIDS=()

for i in "${!NAMES[@]}"; do
	name="${NAMES[$i]}"
	./"$name" --headless > "run-logs/$name.stdout.log" 2>&1 &
	PIDS[$i]=$!
	echo "started $name (pid ${PIDS[$i]})"
	sleep "${DELAYS[$i]}"
done

sleep "$RUN_SECONDS"

echo "== listening sockets"
if command -v ss > /dev/null 2>&1; then
	ss -ltnpu 2>/dev/null | tee run-logs/listening.txt
else
	lsof +c 0 -nP -iTCP -sTCP:LISTEN -iUDP 2>/dev/null | grep -E "$(IFS='|'; echo "${NAMES[*]}")" | tee run-logs/listening.txt
fi

echo "== process state"
: > run-logs/process-state.txt
for i in "${!NAMES[@]}"; do
	name="${NAMES[$i]}"
	if kill -0 "${PIDS[$i]}" 2>/dev/null; then
		echo "$name: running" | tee -a run-logs/process-state.txt
	else
		wait "${PIDS[$i]}"
		echo "$name: EXITED with $?" | tee -a run-logs/process-state.txt
	fi
done

echo "== stopping"
for ((i = ${#NAMES[@]} - 1; i >= 0; i--)); do
	kill -INT "${PIDS[$i]}" 2>/dev/null || true
	sleep 2
done
sleep 5
for i in "${!NAMES[@]}"; do
	kill -KILL "${PIDS[$i]}" 2>/dev/null && echo "${NAMES[$i]} needed SIGKILL"
done

for name in "${NAMES[@]}"; do
	echo "===== $name stdout (tail)"
	tail -n 40 "run-logs/$name.stdout.log"
done

# Startup row counts, one "<server> <table> <rows>" per line, for cross-backend comparison.
for name in "${NAMES[@]}"; do
	grep -ho "recordset_loader: [A-Za-z0-9_]*: [0-9]* rows" "run-logs/$name.stdout.log" 2>/dev/null |
		awk -v s="$name" '{gsub(":", "", $2); print s, $2, $3}'
done | sort > run-logs/startup-row-counts.txt
echo "== startup row counts ($(wc -l < run-logs/startup-row-counts.txt) tables)"

grep -q EXITED run-logs/process-state.txt && exit 1
exit 0
