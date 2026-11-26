# Negative tests for pr-gate

Each gate here has a negative-test suite: it feeds a known-bad input through the real command line and
asserts the FAIL tag and exit code, and for each check it also builds a weakened copy of the script that
drops that check, to prove the suite fails when the check is gone. This is the standard the scripts are
held to; a gate with no negative test is the failure they were written to catch.

The suites are fixture-heavy (frozen PR snapshots, scratch git repositories) and take a project's own
values as parameters: the repository, the do-not-merge PR numbers, and the required PR numbers. They are
not shipped here as fixtures because they are tens of megabytes of one project's pull-request history. If
you adopt these scripts, port the suite with your own fixtures; the shape is:

## pr-presence.sh (fully generic, no project flags)
Build a base commit, a PR that edits it, and a candidate tree. Assert:
- the PR present at the candidate -> EXACT or PATCH-EQUIVALENT, exit 0;
- absent -> DIFFERS/MISSING, exit 1;
- a `merge=union` attribute from the repo's `.gitattributes`, `.git/info/attributes`, a template, or
  `GIT_ATTR_SOURCE`, and a custom `merge.*.driver`, must NOT flip a missing PR to present;
- a conflicted path must never count as present.

## check-inventory.sh
Build a manifest and a frozen `--live-json`/`--live-total` snapshot. Assert:
- every open PR has exactly one row and the totals match -> IN SYNC, exit 0;
- a missing row, an extra row, a wrong head, or a total that disagrees -> the matching FAIL, exit 1;
- a hidden SHA inside an HTML comment does not pass.

## compose-base.sh
Build a manifest, a snapshot, and scratch commits (an EXCLUDE PR, a PR stacked on it, an ALTERNATIVE on a
do-not-merge head). Supply `--repo`, `--required` and `--dnm`. Assert:
- a clean plan -> PLAN CLEAN, exit 0; a conflict -> FAIL[conflict], exit 1;
- a required PR not INCLUDE/BLOCKED -> FAIL[required]; a do-not-merge PR not EXCLUDE/BLOCKED ->
  FAIL[dnm]; an excluded/do-not-merge head entering by ancestry -> FAIL[excluded-ancestor];
- `--apply` builds a worktree whose tree equals the plan, or FAIL[drift].

Each suite exits 0 only if every case matched. Run under a non-interactive git (`GIT_EDITOR=true`) and in
a repository with no custom merge drivers.
