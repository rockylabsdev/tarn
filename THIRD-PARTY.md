# Third-party files shipped in Tarn

| What | Where | License |
|---|---|---|
| Tabler Icons (the SVGs Tarn uses), Copyright (c) 2020-2026 Pawel Kuna | `src/ui/assets/icons/`, license text in `src/ui/assets/icons/LICENSE` | MIT |
| Geist and Geist Mono fonts (Regular, Medium, SemiBold), Copyright 2024 The Geist Project Authors | `src/ui/assets/fonts/`, license text in `src/ui/assets/fonts/OFL.txt` | SIL Open Font License 1.1 |
| Emoji names and search words (1715 emoji), from GitHub's gemoji database, Copyright (c) 2013 GitHub, Inc. | `src/ui/assets/emoji.tsv` | MIT |
| 20 background images (`assets/backgrounds/*.jpg`): created by Jamey Smith with ChatGPT (OpenAI); released with Tarn under the MIT license. | `src/core/frame/assets/backgrounds/` | MIT |
| 23 agent skills from [mattpocock/skills](https://github.com/mattpocock/skills), Copyright (c) 2026 Matt Pocock: `code-review`, `codebase-design`, `diagnosing-bugs`, `domain-modeling`, `grill-me`, `grill-with-docs`, `grilling`, `handoff`, `implement`, `implement-spec`, `pr`, `prototype`, `research`, `retro`, `setup-matt-pocock-skills`, `tdd`, `teach`, `to-spec`, `to-tickets`, `triage`, `wait-what`, `wayfinder`, `writing-for-agents`. Exact versions are in `skills-lock.json`. | `.claude/skills/<name>/`, license text in `.claude/skills/LICENSES/mattpocock-skills-MIT.txt` | MIT |
| The Summary section's menu of visuals in the `pr` skill, from the `show-me` skill in [humanlayer/skills](https://github.com/humanlayer/skills), Copyright (c) 2026 HumanLayer | `.claude/skills/pr/SKILL.md`, credit in `.claude/skills/pr/CREDITS.md`, license text in `.claude/skills/LICENSES/humanlayer-skills-MIT.txt` | MIT |

These are development aids for coding agents. They are not compiled into the `tarn` binary or installed by the package.

The fonts, icons, emoji list and background images are compiled into the `tarn` binary as Qt resources. The style values (radii, sizes) are in `src/ui/qml/Style.qml`; colors are read at run time from the Palette file, if one is set.
