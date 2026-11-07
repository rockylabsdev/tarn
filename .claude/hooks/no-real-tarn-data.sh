#!/bin/sh
# PreToolUse hook (Bash): asks the user before a command removes or moves files in the
# real Tarn folders. An agent once cleaned a bench's leftovers out of the real History
# with a wildcard, and the deleted files could not be audited. Scratch copies, such as
# $scratch/home/.local/share/tarn, pass: the path has to start at the real home.
cmd=$(jq -r '.tool_input.command // empty')
home='(~|\$HOME|\$\{HOME\}|/home/[^/[:space:]]+)'
printf '%s\n' "$cmd" | grep -qE "(^|[[:space:]'\"=])($home/\.(local/share|local/state|config|cache)/tarn|\\\$\{?XDG_(DATA|STATE|CONFIG|CACHE)_HOME\}?/tarn)([/[:space:]'\"]|\$)" || exit 0
printf '%s\n' "$cmd" | grep -qE '(^|[;&|( ]|then |do )(rm|mv|shred|unlink|truncate|trash-put)[[:space:]]|gio trash|find .*-delete' || exit 0
jq -n '{hookSpecificOutput: {hookEventName: "PreToolUse", permissionDecision: "ask",
  permissionDecisionReason: "This removes or moves files in the real Tarn folders. If an agent left files there, it should stop and report them instead."}}'
