#!/usr/bin/env bash
# The wayfinder frontier: the map body, then its open, unblocked, unclaimed child tickets in map
# order, one "number<TAB>type<TAB>title" line each. The first line is the next ticket to claim.
#
#   scripts/frontier.sh <map number>
#
# Reads the map's own sub-issues, never all open issues: the repo also holds build tickets, which
# are not on the frontier. The repo comes from the current checkout's remote.
set -euo pipefail

map=${1:?usage: scripts/frontier.sh <map number>}
repo=$(gh repo view --json nameWithOwner --jq .nameWithOwner)

gh issue view "$map" --json title,state,body --jq '"# \(.title) (#'"$map"', \(.state))\n\n\(.body)"'
echo
echo "## Frontier"
gh api "repos/$repo/issues/$map/sub_issues" --paginate \
    --jq '.[] | select(.state == "open" and .issue_dependencies_summary.blocked_by == 0 and (.assignees | length) == 0)
          | "\(.number)\t\([.labels[].name | select(startswith("wayfinder:")) | ltrimstr("wayfinder:")] | join(","))\t\(.title)"'
