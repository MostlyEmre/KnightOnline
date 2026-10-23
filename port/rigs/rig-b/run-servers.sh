#!/usr/bin/env bash
# Starts the five servers headless in upstream's documented order, lets them run,
# records what they listen on and whether each reached READY, then stops them.
# Usage: run-servers.sh <bin dir>
set -uo pipefail

BIN_DIR="$1"
cd "$BIN_DIR"
mkdir -p run-logs

declare -A PIDS
start() {
	local name="$1"
	./"$name" --headless > "run-logs/$name.stdout.log" 2>&1 &
	PIDS[$name]=$!
	echo "started $name (pid ${PIDS[$name]})"
	sleep "$2"
}

start AIServer 20
start Ebenezer 20
start Aujard 10
start VersionManager 5
start ItemManager 5

sleep 30

echo "== listening sockets"
(ss -ltnpu || netstat -ltnpu) 2>/dev/null | tee run-logs/listening.txt

echo "== process state"
for name in AIServer Ebenezer Aujard VersionManager ItemManager; do
	if kill -0 "${PIDS[$name]}" 2>/dev/null; then
		echo "$name: running"
	else
		wait "${PIDS[$name]}"
		echo "$name: EXITED with $?"
	fi
done | tee run-logs/process-state.txt

echo "== stopping"
for name in ItemManager VersionManager Aujard Ebenezer AIServer; do
	kill -INT "${PIDS[$name]}" 2>/dev/null || true
	sleep 2
done
sleep 5
for name in "${!PIDS[@]}"; do
	kill -KILL "${PIDS[$name]}" 2>/dev/null && echo "$name needed SIGKILL"
done

for name in AIServer Ebenezer Aujard VersionManager ItemManager; do
	echo "===== $name stdout (tail)"
	tail -n 60 "run-logs/$name.stdout.log"
done

grep -q EXITED run-logs/process-state.txt && exit 1
exit 0
