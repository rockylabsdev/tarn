# Curved arrows

Read when working on an arrow's bend.

- An arrow stores a signed `bend` (px off the tail-to-head line); `Annotation::bendHandle()/dragBend()/straighten()` are the hooks, Handle number 2 (`Annotation::kBendHandle`) in `chrome()`. `EditorSession::straightenAt(x, y)` is the double-click. `tests/curved_arrows` (references in `tests/curved_arrows/reference/`, regenerate as verify.md section 2 says) drives it with pointer calls; the Project file round trip is in `tests/project`.
