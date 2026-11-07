#!/usr/bin/env python3
"""Edit a GitHub PR body, issue body or comment in place, the safe way.

The user ticks needs-eyes checklists by hand on GitHub, so a PATCH of a local copy
wipes their ticks. This edits the LIVE text instead: it fetches it, changes only what
you name, and refuses to save if anyone edited it in the meantime.

  scripts/gh-edit.py <target> --show                  print the live text
  scripts/gh-edit.py <target> --tick <text>           tick the one unticked item containing <text>
  scripts/gh-edit.py <target> --replace <old> <new>   swap the text in file <old> (found exactly
                                                      once) for the text in file <new>
  scripts/gh-edit.py <target> --append <file>         add a file's text at the end

<target> is pr:<number>, issue:<number> or comment:<id> (an issue or PR comment), in the
repo of the current checkout. Write <old>, <new> and <file> with the Write tool.
"""
import json
import subprocess
import sys

PATHS = {"pr": "repos/{owner}/{repo}/pulls/", "issue": "repos/{owner}/{repo}/issues/",
         "comment": "repos/{owner}/{repo}/issues/comments/"}


def fail(message):
    sys.exit(f"gh-edit: {message}")


def api(path, *args, payload=None):
    result = subprocess.run(["gh", "api", path, *args], input=payload, capture_output=True, text=True)
    if result.returncode != 0:
        fail(result.stderr.strip() or f"gh api {path} failed")
    return json.loads(result.stdout)


def fetch(path):
    item = api(path)
    return item.get("body") or "", item["updated_at"]


def main(argv):
    if len(argv) < 2 or ":" not in argv[0]:
        sys.exit(__doc__)
    kind, _, ident = argv[0].partition(":")
    if kind not in PATHS or not ident.isdigit():
        fail(f"target {argv[0]!r} is not pr:<n>, issue:<n> or comment:<id>")
    path = PATHS[kind] + ident
    action, args = argv[1], argv[2:]
    body, updated = fetch(path)

    if action == "--show" and not args:
        print(body)
        return
    if action == "--tick" and len(args) == 1:
        lines = body.split("\n")
        hits = [i for i, line in enumerate(lines) if line.lstrip().startswith("- [ ]") and args[0] in line]
        if len(hits) != 1:
            fail(f"{len(hits)} unticked items contain {args[0]!r}; name exactly one")
        lines[hits[0]] = lines[hits[0]].replace("- [ ]", "- [x]", 1)
        new_body = "\n".join(lines)
    elif action == "--replace" and len(args) == 2:
        old, new = (open(f, encoding="utf-8").read() for f in args)
        count = body.count(old)
        if count != 1:
            fail(f"the text in {args[0]} is in the live text {count} times, not once. "
                 "Run --show and copy the lines as they are now (the user may have ticked some)")
        new_body = body.replace(old, new, 1)
    elif action == "--append" and len(args) == 1:
        new_body = body.rstrip("\n") + "\n\n" + open(args[0], encoding="utf-8").read()
    else:
        sys.exit(__doc__)

    if fetch(path)[1] != updated:
        fail("someone edited it while this ran; nothing saved. Run the command again")
    api(path, "-X", "PATCH", "--input", "-", payload=json.dumps({"body": new_body}))
    changed = sum(a != b for a, b in zip(body.split("\n"), new_body.split("\n")))
    changed += abs(len(body.split("\n")) - len(new_body.split("\n")))
    print(f"gh-edit: saved {argv[0]} ({changed} lines changed)")


if __name__ == "__main__":
    main(sys.argv[1:])
