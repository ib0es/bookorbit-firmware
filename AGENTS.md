# Agent instructions

## Git commits

- Follow Conventional Commits 1.0.0: `<type>(<scope>): <description>`.
- Types: feat, fix, refactor, perf, test, docs, build, ci, chore, style, revert.
- Scope is optional; use the affected subsystem (e.g. reader, ui, settings).
- Write commit messages in English. Use a concise, imperative subject.
- Mark breaking changes with `!` and explain them in a `BREAKING CHANGE:` footer.
- Commit only when explicitly requested. Do not amend commits or rewrite history unless requested.
- Before committing, run `npm ci` once, then check the message with `printf '%s\n' 'feat(reader): add status bar options' | npm run commitlint` (replace the example).
- Check a branch with `npm run commitlint -- --from origin/main --to HEAD`.
- CI checks new commits in pull requests and pushes to main; existing history is not retroactively linted.

## README screenshots

- The screenshots in `README.md` live in `assets/screenshots/` and are generated from the simulator. Regenerate them whenever a change alters how a screen looks: UI views, layouts, icons, fonts, strings or translations shown on a pictured screen, the key or touch flow to reach one, or the web portal page.
- Build the simulator, then run the generator: `make -f test/sim/Makefile all` and `python3 tools/gen_screenshots.py` (needs Pillow; the web portal pictures need Google Chrome and are skipped without it).
- Look at the new pictures before committing them, and commit the changed PNGs with the change. Add a scene to `tools/gen_screenshots.py` and a picture to the README when a new screen is worth showing.
