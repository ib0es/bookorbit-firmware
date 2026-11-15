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
