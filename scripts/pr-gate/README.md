# pr-gate: compose and verify a projected-merged base from open pull requests

These scripts answer two questions a project with many in-flight pull requests keeps asking:

1. Given the pull requests that are open right now, what tree would result from merging the ones we
   want? (compose-base.sh)
2. Is this tree actually that composition, and not something with extra or missing commits? (the
   presence and inventory checks)

They were written for benchmark reproducibility: a performance number is only meaningful if the tree it
was measured on is exactly the set of pull requests it claims, with nothing silently added or dropped.
The same checks catch a stale PR list, an excluded PR that creeps back in through a stacked branch, and a
"draft" composition that quietly built a real worktree.

Nothing here is specific to this project: the repository, the do-not-merge PR numbers, and the required
PR numbers are all flags.

## The three scripts

### check-inventory.sh
Verifies a manifest (one row per open PR) against the live set of open pull requests, with an independent
total so a truncated page is caught. Filtering a PR list by author once hid a maintainer's own open PRs
for weeks; this queries all authors with complete pagination.

    check-inventory.sh --repo OWNER/NAME --inventory manifest.md
    check-inventory.sh --inventory manifest.md --live-json pages.json --live-total total.json

### compose-base.sh
From a reviewed manifest, merges each wanted PR onto a base in a throwaway object store and reports
whether the result is clean. With --apply it then builds a real branch and worktree whose tree must equal
the planned tree. Every open PR needs an explicit row: INCLUDE, EXCLUDE(reason), ALTERNATIVE(#PR|<sha>)
or BLOCKED(clearance). A do-not-merge PR (named by --dnm) may only be EXCLUDE or BLOCKED, and a step is
refused if it carries a do-not-merge, EXCLUDE or BLOCKED head as an ancestor that the base does not have.

    compose-base.sh --repo OWNER/NAME --manifest m.txt [--dnm "N N"] [--required "N N"] \
        [--base REF] [--remote NAME] [--out DIR] [--plan-only]
    compose-base.sh --repo OWNER/NAME --manifest m.txt --apply NEW-BRANCH --worktree NEW-PATH

The default is plan-only and touches no ref, index, worktree or object. --apply writes only a new branch
and a new worktree, with nothing committed on top: the benchmark tree is the --apply output exactly.

### pr-presence.sh
Is one PR's diff present at a candidate tree? It merges the PR into the candidate in a scratch bare store
and reports EXACT, PATCH-EQUIVALENT, DIFFERS or MISSING per path. The merge runs with no attribute source,
no merge driver, and no inherited git configuration, so a repository's own `merge=union` attribute or a
global config cannot fake a "present" verdict.

    pr-presence.sh PR HEAD BASE CANDIDATE-TREE [DISPOSITIONS]

## Known limits
- Ancestry only. A cherry-picked or rebased copy of an excluded PR is not caught by the ancestor check;
  review stacked PRs by content.
- compose-base's --apply uses real merges, so a conflict leaves the worktree for inspection rather than
  cleaning up.

## Tests
`tests/` holds the negative-test suites. Each case feeds a known-bad input through the real command line
and asserts the expected FAIL tag and exit code; each check also has a weakened copy of the script that
drops it, to prove the test fails when the check is gone. A gate with no negative test is the exact
failure these were written to catch. The suites take a project's own flags (repository, do-not-merge and
required PR numbers) as parameters; see `tests/README.md`.
