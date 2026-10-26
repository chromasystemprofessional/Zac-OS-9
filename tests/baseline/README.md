# Baselines

What the build is checked against, so a change that loses something or adds
warnings shows up in review. They were recorded before the 2026 restructuring
(`main` at `01b124c`, after 1.0.21) and change only on purpose.

| File | What | Checked by |
|---|---|---|
| `installed-files.txt` | Every file the package installs, relative to `{prefix}` (`/usr`) or `{sysconfdir}` (`/etc`). | The `installed-files` test. |
| `warnings` | How many distinct compiler warnings CI's package build may print. | CI's "Count compiler warnings" step (`tests/baseline/count_warnings.py`). |

## When a check fails

**A file is "no longer installed"**: something stopped installing it (a moved
source, a renamed target, a lost `install: true`). Fix that, unless the file is
meant to go.

**A file is "newly installed"**, or one is meant to go: update the list, and
the change shows in the pull request:

```sh
python3 tests/baseline/test_installed_files.py build tests/baseline/installed-files.txt --update
```

**More warnings**: CI lists all of them; fix the new one. When a change removes
warnings, CI says so: lower the number in `warnings` to match.

## At the time they were recorded

CI's package build of `01b124c`: 176 tests, 171 passed, 5 skipped (the five CI
allows: `afp-disconnect`, `decor`, `menudraw`, `screen-sharing-session`,
`widgets`); 79 distinct warnings, 71 of them `-Wmissing-field-initializers`.
