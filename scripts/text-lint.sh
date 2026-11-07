#!/bin/sh
# Checks on the words in the repo, for scripts/check.sh. These run on docs too, so
# check.sh calls this before its docs-only shortcut. Files git ignores are not read.
set -u

failed=0
report() {   # report <offending lines> <message>
    [ -z "$1" ] && return
    printf '%s\n' "$1" | cut -c1-160 >&2
    echo "check: $2" >&2
    failed=1
}

# American spelling. The exempt files are the readers of names older saved files use,
# the upstream skills, and third-party text.
british='\b[a-z_]*(colour|centre|behaviour|grey|cancell(ed|ing)|favourite|neighbour|honour|licence|organis|recognis|initialis|normalis|serialis|customis|analys(e|ing)\b|artefact|judgement|fulfil\b|catalogue|defence|metre|modell)[a-z_]*'
hits=$(git grep --untracked -nIiE "$british" -- . \
    ':!scripts/text-lint.sh' ':!.claude/skills' ':!src/ui/assets/emoji.tsv' ':!tests/saved_data' \
    ':!src/core/annotations/counter.cpp' ':!src/core/annotations/text.cpp' \
    ':!src/core/document/annotation_json.h' ':!src/core/frame/frame.cpp' \
    ':!src/core/state/remembered_state.cpp')
report "$hits" "use American spelling; a saved older name is read in one place per file format (CODING_STANDARDS.md)"

# The repo is public. The username comes from the machine so it stays out of this file.
hits=$(git grep --untracked -nIF -e "/home/$(id -un)/" -- . ':!scripts/text-lint.sh')
report "$hits" "a path into this machine's home folder; use ~ or a made-up user"
hits=$(git grep --untracked -nIi -e '@gmail\.com' -- . ':!scripts/text-lint.sh')
report "$hits" "a personal email address; the repo uses the GitHub noreply one"

# Tarn is described on its own terms. The planning briefs name their references.
hits=$(git grep --untracked -nIi -e '[c]leanshot' -- . ':!docs/planning')
report "$hits" "another product's name; describe Tarn on its own terms"

exit "$failed"
