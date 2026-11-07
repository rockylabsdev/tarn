#!/bin/sh
# PreToolUse hook (Bash): blocks `pkill -f <pattern>` / `pgrep -f`-driven kills whose
# pattern also matches the agent's own shell. The shell running the command holds
# the pattern in its own command line, so the kill ends the tool call (exit 144).
cmd=$(jq -r '.tool_input.command // empty')
printf '%s\n' "$cmd" | grep -qE '(^|[;&|( ]|then |do )pkill([[:space:]]+-[A-Za-z0-9]+)*[[:space:]]+-[A-Za-z]*f' || exit 0
# A bracketed first letter ('[q]ml6 Editor.qml') matches the process but not its own text.
printf '%s\n' "$cmd" | grep -qE "pkill([[:space:]]+-[A-Za-z0-9]+)*[[:space:]]+['\"]?\[" && exit 0
cat >&2 <<'MSG'
Blocked: `pkill -f <pattern>` also matches this Bash call's own shell, so it kills the tool call itself (exit 144).
Stop the process by its PID instead: start it with `cmd & echo $! > /tmp/<name>.pid`, stop it with `kill "$(cat /tmp/<name>.pid)"`.
Or bracket the pattern's first letter so it can't match its own text: pkill -f '[q]ml6 Editor.qml'.
MSG
exit 2
