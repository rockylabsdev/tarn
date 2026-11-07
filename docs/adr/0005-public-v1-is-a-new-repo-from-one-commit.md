# Public v1 is a new repo from one commit

Tarn goes public as a new repository at `rockylabsdev/tarn`, starting from a single commit tagged `v1.0.0`, and the private development repo is frozen as `tarn-archive` with its issues, pull requests and commits. Rewriting the old repo in place was the first plan, but its history held third-party screenshots that a force-push cannot fully remove: pull requests keep the old commits, and only GitHub Support can delete them, which its policy rules out for non-sensitive data.

The cost is that the tracker from before v1 stays private and the public log starts at v1.0.0. The v1, v2 and public v1 specs are kept in `docs/planning/`, and planning is public from then on. Nothing is developed in `tarn-archive` after the move, so there are never two repos to keep in sync.
