#!/usr/bin/env bash
#
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# pr-presence.sh PR HEAD BASE CANDIDATE [DISPOSITIONS]
#
# Is PR's content present in CANDIDATE?  Run inside the repo.  Read-only git only
# (rev-parse, merge-base, diff-tree, ls-tree, merge-tree into a throwaway object dir); the working
# tree is never read and no object is written to the repository.
#
# Every path in `git diff-tree -r --raw BASE HEAD` (adds, edits, deletes, gitlinks, mode
# and type changes; nothing is skipped) gets one verdict:
#   EXACT             candidate mode + blob/gitlink SHA == HEAD's (deleted path: absent in candidate)
#   PATCH-EQUIVALENT  merging the PR into the candidate is a no-op at this path:
#                     `git merge-tree --write-tree --merge-base=BASE CANDIDATE HEAD` leaves the candidate's
#                     entry (mode, type, sha) at the path unchanged.  Location-sensitive (a guard in the
#                     wrong copy of an identical function DIFFERS), and other PRs' edits to the same file
#                     do not hide the PR's change.
#   DIFFERS           present but neither of the above
#   MISSING           absent from candidate, or deleted by the PR but still present
# DIFFERS/MISSING fail unless DISPOSITIONS has a line `#PR path ALTERNATIVE|EXCLUDE reason...`
# (`PR` may be written with or without `#`; reason must be non-empty).  Blank lines and lines
# starting `# ` are comments.  Any other line is malformed and an error.
#
# Exit 0 present (EXACT/PATCH-EQUIVALENT, or reviewed dispositions), 1 not present, 2 cannot
# evaluate (usage, bad object, empty PR diff, malformed or duplicate disposition).
# Every failure prints a FAIL[tag] line.
#
# The previous version (backup pr-presence.sh.pre-repair-20261007) grepped the worktree for the
# PR's added lines longer than 20 chars and certified >=95% as PRESENT.  It skipped gitlinks,
# manifests, patches, deletions and modes, could not see a short guard line, and always exited 0.
# No line-match percentage is computed here.
#
# ponytail: disposition paths are whitespace-delimited, so a path containing whitespace cannot be
# dispositioned (it fails closed).  Add a quoted form if such a path ever needs one.
# Copyright (C) 2026 Greg Burd
set -uo pipefail
export GIT_OPTIONAL_LOCKS=0 GIT_LITERAL_PATHSPECS=1 GIT_EDITOR=true
fail() { printf 'FAIL[%s] %s\n' "$1" "$2"; }
die()  { fail "$1" "$2"; exit 2; }

{ [ $# -eq 4 ] || [ $# -eq 5 ]; } || die usage "pr-presence.sh PR HEAD BASE CANDIDATE [DISPOSITIONS]"
PR=${1#\#}; [[ $PR =~ ^[0-9]+$ ]] || die usage "PR must be a number: $1"
git rev-parse --git-dir >/dev/null 2>&1 || die usage "not inside a git repository"
cd "./$(git rev-parse --show-cdup)" || die usage "cannot cd to the repository top"
H=$(git rev-parse -q --verify "$2^{commit}") || die object "head '$2' is not a commit"
B=$(git rev-parse -q --verify "$3^{commit}") || die object "base '$3' is not a commit"
C=$(git rev-parse -q --verify "$4^{tree}")   || die object "candidate '$4' is not a tree-ish"
git merge-base --is-ancestor "$B" "$H" \
    || echo "WARN base is not an ancestor of head: the diff also reverts base-only changes"

declare -A DISP
if [ $# -eq 5 ]; then
    { [ -f "$5" ] && [ -r "$5" ]; } || die disposition "cannot read $5"
    ln=0
    while IFS= read -r l || [ -n "$l" ]; do
        ln=$((ln + 1))
        [[ $l =~ ^[[:space:]]*$ || $l =~ ^#([[:space:]]|$) ]] && continue
        [[ $l =~ ^#?([0-9]+)[[:space:]]+([^[:space:]]+)[[:space:]]+(ALTERNATIVE|EXCLUDE)[[:space:]]+(.*[^[:space:]].*)$ ]] \
            || die disposition "$5:$ln malformed (want '#PR path ALTERNATIVE|EXCLUDE reason'): $l"
        k="${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
        [ -z "${DISP[$k]+x}" ] || die disposition "$5:$ln duplicate disposition for #$k"
        DISP[$k]="${BASH_REMATCH[3]}: ${BASH_REMATCH[4]}"
    done < "$5"
fi

RAW=$(mktemp) || die usage "mktemp failed"
OBJ=$(mktemp -d) || die usage "mktemp -d failed"; trap 'rm -f "$RAW" "$OBJ.out"; find "$OBJ" -depth -delete' EXIT
git diff-tree -r -z --raw --no-renames "$B" "$H" > "$RAW" || die object "diff-tree $B $H failed"
[ -s "$RAW" ] || die empty "base..head has no changes; nothing to verify"

# REVIEW F7: merge the PR into the candidate once; new objects go to $OBJ only (repo objects are read via alternates)
GD=$(git rev-parse --absolute-git-dir) && OD=$(git rev-parse --git-path objects) && OD=$(cd "$OD" && pwd) \
    || die object "cannot locate the object directory"
MTO=$OBJ.out
# REREVIEW LOW: a merge attribute (merge=union is built in and needs no driver config; a custom driver can pick a
# side) from the checkout's .gitattributes, info/attributes or core.attributesFile can fake PRESENT. Merge in a
# fresh bare store (no worktree, no info/attributes) that reads the repository's objects through alternates, with
# core.attributesFile and attr.tree pinned empty, global/system config ignored, and custom drivers refused.
drv=$(git config --get-regexp '^merge\..*\.driver$' 2>/dev/null) && die config "custom merge driver(s) configured, merge-tree result cannot be trusted: $(head -c 200 <<<"$drv" | tr '\n' ' ')"
MS=$OBJ/store.git
env -u GIT_DIR -u GIT_COMMON_DIR -u GIT_WORK_TREE -u GIT_OBJECT_DIRECTORY git init -q --bare --template= "$MS" >/dev/null 2>&1 && mkdir -p "$MS/objects/info" \
    && printf '%s\n' "$OD" ${GIT_ALTERNATE_OBJECT_DIRECTORIES:+$(tr ':' '\n' <<<"$GIT_ALTERNATE_OBJECT_DIRECTORIES")} >"$MS/objects/info/alternates" \
    && [ ! -e "$MS/info/attributes" ] || die object "cannot create the scratch merge store"
ET=4b825dc642cb6eb9a060e54bf8d69288fbee4904
env -u GIT_DIR -u GIT_COMMON_DIR -u GIT_WORK_TREE -u GIT_INDEX_FILE -u GIT_OBJECT_DIRECTORY -u GIT_ALTERNATE_OBJECT_DIRECTORIES -u GIT_ATTR_SOURCE \
    -u GIT_CONFIG_PARAMETERS -u GIT_CONFIG_COUNT GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null GIT_ATTR_NOSYSTEM=1 \
    git --git-dir="$MS" -c core.attributesFile=/dev/null -c attr.tree=$ET -c merge.renames=false \
    merge-tree --write-tree -z --name-only --no-messages -X no-renames \
    --merge-base="$B" "$C" "$H" >"$MTO"; mrc=$?
IFS= read -r -d '' MT <"$MTO"   # -z: <tree>NUL, then (rc 1) one conflicted path per NUL
{ [ $mrc -le 1 ] && [[ $MT =~ ^[0-9a-f]{40}([0-9a-f]{24})?$ ]]; } || die object "git merge-tree --merge-base=$B $C $H failed (rc $mrc)"
declare -A CONFL
while IFS= read -r -d '' cp; do CONFL[$cp]=1; done < <(tail -c +$((${#MT} + 2)) "$MTO")
ment() { env -u GIT_COMMON_DIR -u GIT_OBJECT_DIRECTORY git --git-dir="$MS" ls-tree --full-tree -z "$MT" -- "$1" \
         | tr -d '\0'; }
declare -A CNT; bad=0; total=0
while IFS= read -r -d '' meta && IFS= read -r -d '' path; do
    total=$((total + 1))
    read -r _ dmode _ dsha _ <<< "${meta#:}"
    ent=$(git ls-tree --full-tree -z "$C" -- "$path" | tr -d '\0') || die object "ls-tree $C failed at $path"
    cmode=; ctype=; csha=
    [ -n "$ent" ] && [ "${ent#*$'\t'}" = "$path" ] && read -r cmode ctype csha <<< "${ent%%$'\t'*}"
    [ "$ctype" = tree ] && ctype=        # a directory where the PR has a file is not that file
    if [ "$dmode" = 000000 ]; then
        if [ -z "$ctype" ]; then s=EXACT; else s=MISSING; fi
    elif [ -z "$ctype" ]; then s=MISSING
    elif [ "$cmode" = "$dmode" ] && [ "$csha" = "$dsha" ]; then s=EXACT
    elif ! m=$(ment "$path"); then die object "ls-tree of the merged tree failed at $path"
    elif [ -z "${CONFL[$path]+x}" ] && [ "$m" = "$ent" ]; then s=PATCH-EQUIVALENT   # merging the PR is a no-op here
    else s=DIFFERS
    fi
    k="$PR $path"
    if [[ $s = DIFFERS || $s = MISSING ]] && [ -n "${DISP[$k]+x}" ]; then
        printf '%s\t%s\tDISPOSITIONED %s\n' "$s" "$path" "${DISP[$k]}"; s=DISPOSITIONED
    elif [[ $s = DIFFERS || $s = MISSING ]]; then
        printf '%s\t%s\n' "$s" "$path"; fail "${s,,}" "#$PR $path"; bad=$((bad + 1))
    else
        printf '%s\t%s\n' "$s" "$path"
    fi
    CNT[$s]=$(( ${CNT[$s]:-0} + 1 ))
done < "$RAW"

printf '#%s paths=%d exact=%d patch-equivalent=%d dispositioned=%d differs=%d missing=%d\n' "$PR" "$total" \
    "${CNT[EXACT]:-0}" "${CNT[PATCH-EQUIVALENT]:-0}" "${CNT[DISPOSITIONED]:-0}" "${CNT[DIFFERS]:-0}" "${CNT[MISSING]:-0}"
if [ "$bad" -gt 0 ]; then echo "RESULT #$PR NOT-PRESENT ($bad path(s) DIFFERS/MISSING without disposition)"; exit 1; fi
if [ "${CNT[DISPOSITIONED]:-0}" -gt 0 ]; then echo "RESULT #$PR PRESENT-WITH-DISPOSITIONS"; else echo "RESULT #$PR PRESENT"; fi
exit 0
