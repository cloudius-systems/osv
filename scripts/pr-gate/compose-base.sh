#!/usr/bin/env bash
#
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# compose-base.sh -- PLAN (default) or build a projected-merged base:
#   <base> + every open PR (ALL authors) that an explicit, reviewed disposition manifest marks INCLUDE
#   (or replaces with a reviewed ALTERNATIVE commit), merged in MANIFEST ROW ORDER. That order is the
#   reviewed dependency order; there is no dependency solver.
#
# Usage:
#   compose-base.sh --manifest FILE [--prs-json PAGES.json --prs-total TOTAL.json]
#                           [--base REF] [--remote NAME] [--out NEWDIR] [--plan-only]
#   build-projected-base.sh <same> --apply NEW-BRANCH --worktree NEW-PATH
#
#   --plan-only (default): ZERO mutations of the repository (refs, index, worktrees, objects). PR heads are
#       fetched into a scratch bare store under NEWDIR that borrows the repo's objects via alternates, and
#       every step is checked with `git merge-tree --write-tree` there. Writes NEWDIR/plan.txt.
#   --apply: only after a clean, fully composable plan. Refuses an existing branch name or worktree path.
#       Creates the branch with `git worktree add -b` (no force) and merges the planned SHAs in order.
#       It never forces, resets or cleans up: on failure the worktree is left as-is for review.
#   --prs-json/--prs-total: frozen snapshot (both or neither). Without them, two GET calls are made and
#       saved as NEWDIR/prs.json and NEWDIR/total.json. Freeze a snapshot to write a manifest against:
#         gh api --paginate --slurp 'repos/OWNER/NAME/pulls?state=open&per_page=50&sort=created&direction=asc' >PAGES.json
#         gh api 'search/issues?q=repo:OWNER/NAME+is:pr+is:open&per_page=1' >TOTAL.json
#   --base (default upstream/master) is resolved ONCE to a commit. --remote (default upstream) is
#       the remote URL that serves refs/pull/N/head. --out must not exist. The default is a new mktemp dir.
#       Receipts are always kept.
#
# Manifest: exactly one row per open PR in the snapshot. Rows look like this:
#     #<N> <40-hex head> INCLUDE
#     #<N> <40-hex head> EXCLUDE(<full reason>)
#     #<N> <40-hex head> ALTERNATIVE(#<M>)        covered by INCLUDE row #M (nothing merged here)
#     #<N> <40-hex head> ALTERNATIVE(<40-hex>)    reviewed replacement commit, merged at this position
#     #<N> <40-hex head> BLOCKED(<clearance>)     allowed in a plan; composition is refused (exit 3)
#   Blank lines and lines starting with '#' followed by a non-digit are comments. The head must equal
#   the snapshot head AND the fetched refs/pull/N/head. Draft state is metadata only, never a disposition.
#   A PR named by --required must be INCLUDE or BLOCKED. A do-not-merge PR (named by --dnm, plus
#   every snapshot PR whose title or a label says do-not-merge or DNM) must be EXCLUDE or BLOCKED. ALTERNATIVE(<sha>)
#   must not be the row's own head or the head of any do-not-merge PR.
#
# Exit: 0 plan clean and composable (or --apply built). 1 fetch/object/head-moved/conflict/merge failure.
#       2 usage/snapshot/manifest error: rejected before any fetch. 3 plan clean but BLOCKED rows remain.
# Every failure prints a `FAIL[tag]` line (also in NEWDIR/failures.txt).
#
set -uo pipefail
export LC_ALL=C GIT_OPTIONAL_LOCKS=0
REPO=
PER_PAGE=50
REQUIRED=""       # PR numbers that must be INCLUDE or BLOCKED (space-separated)
DNM=""            # do-not-merge PR numbers (space-separated); also any PR whose title/label says DNM
DNM_RE='do[ _-]?not[ _-]?merge|(^|[^a-z0-9])dnm([^a-z0-9]|$)'
BASE_REF=upstream/master REMOTE=upstream
MANIFEST='' PRS_JSON='' PRS_TOTAL='' OUT='' APPLY='' WT='' PLAN_ONLY=''

usage() { echo "FAIL[usage]: $*"; exit 2; }
while [ $# -gt 0 ]; do
    case $1 in
        --manifest|--prs-json|--prs-total|--base|--remote|--out|--apply|--worktree|--repo|--dnm|--required)
            [ $# -ge 2 ] || usage "$1 needs a value" ;;&
        --repo)      REPO=$2; shift 2 ;;
        --dnm)       DNM=$2; shift 2 ;;
        --required)  REQUIRED=$2; shift 2 ;;
        --manifest)  MANIFEST=$2; shift 2 ;;
        --prs-json)  PRS_JSON=$2; shift 2 ;;
        --prs-total) PRS_TOTAL=$2; shift 2 ;;
        --base)      BASE_REF=$2; shift 2 ;;
        --remote)    REMOTE=$2; shift 2 ;;
        --out)       OUT=$2; shift 2 ;;
        --apply)     APPLY=$2; shift 2 ;;
        --worktree)  WT=$2; shift 2 ;;
        --plan-only) PLAN_ONLY=1; shift ;;
        *) usage "unknown argument '$1'" ;;
    esac
done
[ -n "$MANIFEST" ] || usage "--manifest FILE is required"
[ -n "$REPO" ] || [ -n "$PRS_JSON" ] || usage "--repo OWNER/NAME is required unless --prs-json gives a frozen snapshot"
if [ -n "$PRS_JSON$PRS_TOTAL" ] && { [ -z "$PRS_JSON" ] || [ -z "$PRS_TOTAL" ]; }; then
    usage "--prs-json and --prs-total go together (without the total a truncated page cannot be detected)"
fi
if [ -n "$APPLY$WT" ]; then
    [ -z "$PLAN_ONLY" ] || usage "--plan-only and --apply are exclusive"
    [ -n "$APPLY" ] && [ -n "$WT" ] || usage "--apply and --worktree go together"
fi
git rev-parse --git-dir >/dev/null 2>&1 || usage "run inside the repository"
OBJ=$(git rev-parse --path-format=absolute --git-common-dir)/objects

if [ -n "$OUT" ]; then
    mkdir -- "$OUT" 2>/dev/null || { echo "FAIL[out]: $OUT exists or cannot be created (receipts are never overwritten)"; exit 2; }
else
    OUT=$(mktemp -d "${TMPDIR:-/tmp}/pmb-plan.XXXXXX") || { echo "FAIL[out]: mktemp -d"; exit 2; }
fi
OUT=$(cd -- "$OUT" && pwd) || exit 2
PLAN=$OUT/plan.txt
fail=0
bad() { echo "FAIL[$1]: ${*:2}" | tee -a "$OUT/failures.txt"; fail=1; }
echo "receipts: $OUT"

# --- 1. snapshot: all authors, complete pagination, independent total. JSON is read from a PATH.
if [ -z "$PRS_JSON" ]; then
    if ! gh api --paginate --slurp "repos/$REPO/pulls?state=open&per_page=$PER_PAGE&sort=created&direction=asc" \
            >"$OUT/prs.json" 2>"$OUT/gh.err" \
       || ! gh api "search/issues?q=repo:$REPO+is:pr+is:open&per_page=1" >"$OUT/total.json" 2>>"$OUT/gh.err"; then
        bad gh "GitHub GET failed: $(head -c 300 "$OUT/gh.err")"; exit 2
    fi
else
    cp -- "$PRS_JSON" "$OUT/prs.json" && cp -- "$PRS_TOTAL" "$OUT/total.json" \
        || { bad json "cannot read snapshot files"; exit 2; }
fi
# ponytail: same validation as check-inventory.sh; factor into one helper if a third caller appears.
if ! live=$(jq -rs --argjson pp "$PER_PAGE" '
      if length != 1 then error("expected one JSON document, got \(length)") else .[0] end
    | if type != "array" or any(.[]; type != "array") then error("not a slurped array of pages") else . end
    | if length > 0 and (any(.[:-1][]; length != $pp) or (.[-1] | length) < 1 or (.[-1] | length) > $pp)
      then error("page sizes \(map(length)) are not a complete per_page=\($pp) pagination") else . end
    | [.[][]]
    | map(if type == "object" and (.number | type) == "number" and (.draft | type) == "boolean"
             and (.user.login | type) == "string" and ((.head.sha // "") | test("^[0-9a-f]{40}$"))
          then . else error("malformed PR object: \(tostring | .[:80])") end)
    | if (map(.number) | unique | length) != length then error("duplicate PR numbers") else . end
    | .[] | "\(.number) \(.draft) \(.user.login) \(.head.sha) \(
        if ([(.title // "")] + [(.labels // [])[] | .name // ""] | any(test($dnm; "i"))) then 1 else 0 end)"' \
      --arg dnm "$DNM_RE" "$OUT/prs.json" 2>"$OUT/jq.err"); then
    bad json "PR snapshot unusable: $(head -c 300 "$OUT/jq.err")"; exit 2
fi
if ! total=$(jq -rs 'if length == 1 and (.[0].total_count | type) == "number" and .[0].incomplete_results == false
                     then .[0].total_count else error("bad search total") end' "$OUT/total.json" 2>"$OUT/jq.err"); then
    bad json "independent total unusable: $(head -c 300 "$OUT/jq.err")"; exit 2
fi
declare -A Ssha Sdraft Slogin
snap=()
while read -r num dr login sha dnm; do
    [ -n "$num" ] || continue
    Ssha[$num]=$sha Sdraft[$num]=$dr Slogin[$num]=$login; snap+=("$num")
    [ "$dnm" != 1 ] || [[ " $DNM " == *" $num "* ]] || DNM="$DNM $num"
done <<<"$live"
pages=$(jq -r 'map(length) | map(tostring) | join("+")' "$OUT/prs.json")
[ "${#snap[@]}" -ge 10 ] || { bad implausible "snapshot has only ${#snap[@]} PRs - treating as query failure"; exit 2; }
[ "$total" = "${#snap[@]}" ] || { bad total "snapshot has ${#snap[@]} PRs (pages $pages) but independent total is $total - truncated or raced"; exit 2; }

# --- 2. manifest: one row per snapshot PR, explicit disposition, full-text reasons
[ -f "$MANIFEST" ] && [ -s "$MANIFEST" ] || { bad manifest "$MANIFEST is missing or empty"; exit 2; }
cp -- "$MANIFEST" "$OUT/manifest.txt" || { bad manifest "cannot read $MANIFEST"; exit 2; }
declare -A Mhead Mdisp Mline
order=()
row_re='^#([1-9][0-9]*)[[:space:]]+([0-9a-f]{40})[[:space:]]+([^[:space:]](.*[^[:space:]])?)[[:space:]]*$'
ln=0
while IFS= read -r line || [ -n "$line" ]; do
    ln=$((ln + 1))
    [[ $line =~ ^[[:space:]]*$ || $line =~ ^#([^0-9]|$) ]] && continue
    if ! [[ $line =~ $row_re ]]; then bad manifest "line $ln is not '#N <40-hex head> DISPOSITION': $line"; continue; fi
    n=${BASH_REMATCH[1]}
    if [ -n "${Mline[$n]:-}" ]; then bad dup "#$n has rows at lines ${Mline[$n]} and $ln"; continue; fi
    Mline[$n]=$ln Mhead[$n]=${BASH_REMATCH[2]} Mdisp[$n]=${BASH_REMATCH[3]}; order+=("$n")
done <"$MANIFEST"
[ "${#order[@]}" -gt 0 ] || bad manifest "$MANIFEST has no rows"
alt_sha() { local t=${1#ALTERNATIVE(}; t=${t%)}; [[ $t =~ ^[0-9a-f]{40}$ ]] && echo "$t"; }
for n in "${order[@]}"; do
    d=${Mdisp[$n]}
    case $d in
        INCLUDE) ;;
        EXCLUDE\(*\)|BLOCKED\(*\))
            r=${d#*(}; r=${r%)}; [[ $r =~ [^[:space:]] ]] || bad disposition "#$n: '$d' needs a non-empty reason" ;;
        ALTERNATIVE\(*\))
            t=${d#ALTERNATIVE(}; t=${t%)}
            if [[ $t =~ ^#([1-9][0-9]*)$ ]]; then
                [ "${Mdisp[${BASH_REMATCH[1]}]:-}" = INCLUDE ] || bad disposition "#$n: '$d' target is not an INCLUDE row"
            elif ! [[ $t =~ ^[0-9a-f]{40}$ ]]; then
                bad disposition "#$n: '$d' needs ALTERNATIVE(#PR) or ALTERNATIVE(<40-hex commit>)"
            fi ;;
        *) bad disposition "#$n: '$d' is not INCLUDE, EXCLUDE(reason), ALTERNATIVE(#pr|sha) or BLOCKED(clearance)" ;;
    esac
done
for n in "${snap[@]}"; do
    if [ -z "${Mline[$n]:-}" ]; then bad missing "#$n (${Slogin[$n]}, draft=${Sdraft[$n]}) is open but has no manifest row"
    elif [ "${Mhead[$n]}" != "${Ssha[$n]}" ]; then bad head "#$n: manifest head ${Mhead[$n]} != snapshot head ${Ssha[$n]}"; fi
done
for n in "${order[@]}"; do
    [ -n "${Ssha[$n]:-}" ] || bad unknown "#$n (manifest line ${Mline[$n]}) is not an open PR in the snapshot"
done
for n in $REQUIRED; do
    [ -n "${Ssha[$n]:-}" ] || bad required "#$n is REQUIRED but not open in the snapshot (merged? closed?) - re-review REQUIRED"
    case ${Mdisp[$n]:-} in
        INCLUDE|BLOCKED\(*\)) ;;
        *) bad required "#$n is REQUIRED: must be INCLUDE or BLOCKED(clearance), manifest says '${Mdisp[$n]:-no row}'" ;;
    esac
done
for n in $DNM; do   # REVIEW F4: EXCLUDE or BLOCKED only; an ALTERNATIVE would merge it (or stand in for it)
    case ${Mdisp[$n]:-} in
        ''|EXCLUDE\(*\)|BLOCKED\(*\)) ;;
        *) bad dnm "#$n is do-not-merge: must be EXCLUDE(reason) or BLOCKED(clearance), manifest says '${Mdisp[$n]}'" ;;
    esac
done
for n in "${order[@]}"; do
    t=$(alt_sha "${Mdisp[$n]}") || continue
    [ "$t" != "${Mhead[$n]}" ] && [ "$t" != "${Ssha[$n]:-}" ] \
        || bad disposition "#$n: ALTERNATIVE($t) is the PR's own head: that is INCLUDE, review it as INCLUDE"
    for m in $DNM; do
        [ "$t" != "${Mhead[$m]:-}" ] && [ "$t" != "${Ssha[$m]:-}" ] || bad dnm "#$n: ALTERNATIVE($t) is the head of do-not-merge #$m"
    done
done
BASE=$(git rev-parse --verify --quiet "$BASE_REF^{commit}") || bad base "cannot resolve $BASE_REF to a commit"
URL=$(git remote get-url "$REMOTE" 2>/dev/null) || bad remote "no remote '$REMOTE'"
if [ -n "$APPLY" ]; then
    git check-ref-format --branch "$APPLY" >/dev/null 2>&1 || bad usage "'$APPLY' is not a valid branch name"
    ! git show-ref --verify --quiet "refs/heads/$APPLY" || bad exists "branch $APPLY already exists (never overwritten)"
    { [ ! -e "$WT" ] && [ ! -L "$WT" ]; } || bad exists "worktree path $WT already exists"
    wtl=$(git worktree list --porcelain)
    ! grep -qxF "worktree $(realpath -m -- "$WT")" <<<"$wtl" || bad exists "a worktree is registered at $WT"
fi
[ $fail = 0 ] || { echo "REJECTED before any fetch: no ref, worktree or object written"; exit 2; }

{
    echo "# projected-base plan $(date -u +%FT%TZ)"
    echo "base $BASE_REF $BASE"
    echo "snapshot ${#snap[@]} PRs pages $pages total $total sha256 $(sha256sum <"$OUT/prs.json" | cut -c1-64)"
    echo "manifest $MANIFEST sha256 $(sha256sum <"$MANIFEST" | cut -c1-64)"
    echo "remote $REMOTE $URL"
    echo "## dispositions (manifest order; draft/author are metadata only)"
    for n in "${order[@]}"; do echo "#$n ${Mhead[$n]} draft=${Sdraft[$n]} author=${Slogin[$n]} ${Mdisp[$n]}"; done
    echo "## steps"
} >"$PLAN"

# --- 3. fetch frozen heads into a scratch store (alternates -> repo objects; the repo is never written)
S=$OUT/store
# Scratch store only: explicit --git-dir, and env that could redirect it at the real repo is dropped.
# No attribute or merge-driver source may reach the scratch store: a template's merge.default=union, or
# GIT_ATTR_SOURCE/GIT_COMMON_DIR from the caller, would skew merge-tree's conflict detection (as it did in
# pr-presence.sh, REREVIEW-2). Pin an empty template and attributes, and strip the inherited git env.
sg() { env -u GIT_DIR -u GIT_COMMON_DIR -u GIT_WORK_TREE -u GIT_INDEX_FILE -u GIT_OBJECT_DIRECTORY \
       -u GIT_ALTERNATE_OBJECT_DIRECTORIES -u GIT_ATTR_SOURCE GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null \
       git --git-dir="$S" -c gc.auto=0 -c maintenance.auto=false -c core.hooksPath=/dev/null \
          -c core.attributesFile=/dev/null -c attr.tree=4b825dc642cb6eb9a060e54bf8d69288fbee4904 "$@"; }
env -u GIT_DIR -u GIT_COMMON_DIR -u GIT_WORK_TREE -u GIT_OBJECT_DIRECTORY GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null \
    git init -q --bare --template= "$S" && echo "$OBJ" >"$S/objects/info/alternates" || { bad store "cannot create $S"; exit 1; }
steps=()
for n in "${order[@]}"; do
    case ${Mdisp[$n]} in
        INCLUDE)
            if ! sg fetch -q --no-tags --no-write-fetch-head "$URL" "refs/pull/$n/head:refs/pmb/pr/$n" \
                    >"$OUT/fetch-$n.log" 2>&1; then
                bad fetch "#$n: fetching refs/pull/$n/head from $REMOTE failed (receipt fetch-$n.log)"; continue
            fi
            got=$(sg rev-parse --verify --quiet "refs/pmb/pr/$n^{commit}")
            [ "$got" = "${Mhead[$n]}" ] \
                || { bad head "#$n: remote head is now ${got:-none}, frozen manifest/snapshot says ${Mhead[$n]}"; continue; }
            steps+=("$n ${Mhead[$n]}") ;;
        ALTERNATIVE\(*\))
            t=$(alt_sha "${Mdisp[$n]}") || continue
            sg cat-file -e "$t^{commit}" 2>/dev/null \
                || { bad object "#$n: ALTERNATIVE commit $t is not in the repository"; continue; }
            steps+=("$n $t") ;;
    esac
done
if [ $fail != 0 ]; then echo "RESULT FAIL (fetch/object/head) - see failures.txt" >>"$PLAN"; echo "plan: $PLAN"; exit 1; fi

# REREVIEW NEW-1: an EXCLUDE/BLOCKED/DNM head must not enter by ancestry (an INCLUDE stacked on it, or an ALTERNATIVE
# built on it). A head already in BASE is not new content. A head the store lacks is skipped: the store fetched every
# step's full history, so a missing head is not an ancestor of any step. The guard below asks the scratch store, which
# is never shallow, so it does not fire in practice; a shallow repository is refused by the fetch and object checks.
# ponytail: ancestry of the CURRENT heads only. Not caught: a cherry-picked copy of an excluded PR, a step stacked on an
# OLDER push of an excluded PR, and a step stacked on the original head of a row whose disposition is ALTERNATIVE(<sha>).
# The manifest review must check stacking against each row's history (REREVIEW-2.md).
[ "$(sg rev-parse --is-shallow-repository 2>/dev/null)" = false ] \
    || bad excluded-ancestor "store is shallow or unreadable: ancestry cannot be proven"
xs=" $DNM "
for n in "${order[@]}"; do case ${Mdisp[$n]} in EXCLUDE\(*\)|BLOCKED\(*\)) xs="$xs$n " ;; esac; done
for s in "${steps[@]}"; do
    read -r n sha <<<"$s"
    for m in $xs; do
        h=${Ssha[$m]:-}; [ -n "$h" ] && [ "$m" != "$n" ] || continue
        sg cat-file -e "$h^{commit}" 2>/dev/null || continue
        sg merge-base --is-ancestor "$h" "$BASE"; rb=$?
        sg merge-base --is-ancestor "$h" "$sha"; ra=$?
        case $ra$rb in
            01) bad excluded-ancestor "#$n step $sha carries #$m's head $h (${Mdisp[$m]:-do-not-merge})" ;;
            10|11|00) ;;
            *) bad excluded-ancestor "#$n vs #$m: git merge-base rc=$ra/$rb (fail closed)" ;;
        esac
    done
done
if [ $fail != 0 ]; then echo "RESULT FAIL (excluded-ancestor)" >>"$PLAN"; echo "plan: $PLAN"; exit 1; fi

# --- 4. merge-tree each step against base + previous clean steps (scratch commits live only in the store)
cur=$BASE k=0
for s in "${steps[@]}"; do
    read -r n sha <<<"$s"; k=$((k + 1)); r=$OUT/step-$k-$n.merge-tree
    sg merge-tree --write-tree --name-only "$cur" "$sha" >"$r" 2>"$r.err"; rc=$?
    tree=$(head -n1 "$r")
    if [ $rc = 0 ] && [[ $tree =~ ^[0-9a-f]{40}$ ]]; then
        cur=$(GIT_AUTHOR_NAME=pmb-plan GIT_AUTHOR_EMAIL=pmb@plan.invalid GIT_COMMITTER_NAME=pmb-plan \
              GIT_COMMITTER_EMAIL=pmb@plan.invalid sg commit-tree --no-gpg-sign -p "$cur" -p "$sha" \
              -m "plan step $k #$n" "$tree") || { bad merge-tree "step $k #$n: commit-tree failed"; break; }
        echo "STEP $k #$n $sha ${Mdisp[$n]} CLEAN tree=$tree" >>"$PLAN"
    elif [ $rc = 1 ] && [[ $tree =~ ^[0-9a-f]{40}$ ]]; then
        bad conflict "step $k #$n $sha conflicts: $(sed -n '2,/^$/p' "$r" | sort -u | tr '\n' ' ')(receipt $r)"
        echo "STEP $k #$n $sha ${Mdisp[$n]} CONFLICT receipt=$(basename "$r") (later steps computed without it)" >>"$PLAN"
    else
        bad merge-tree "step $k #$n: git merge-tree rc=$rc: $(head -c 200 "$r.err")"
        echo "STEP $k #$n $sha ERROR rc=$rc" >>"$PLAN"
    fi
done
FINAL=$(sg rev-parse "$cur^{tree}")
echo "FINAL steps=$k tree=$FINAL scratch-commit=$cur" >>"$PLAN"
if [ $fail != 0 ]; then echo "RESULT FAIL (conflict/merge-tree)" >>"$PLAN"; echo "plan: $PLAN"; exit 1; fi
blocked=0
for n in "${order[@]}"; do
    case ${Mdisp[$n]} in BLOCKED\(*\)) bad blocked "#$n ${Mdisp[$n]}"; blocked=1 ;; esac
done
if [ $blocked = 1 ]; then
    echo "RESULT PLAN-CLEAN-NOT-COMPOSABLE (BLOCKED rows)" >>"$PLAN"; echo "plan: $PLAN"
    [ -z "$APPLY" ] || echo "NOT building $APPLY: BLOCKED rows remain"
    exit 3
fi
if [ -z "$APPLY" ]; then
    echo "RESULT PLAN-CLEAN" >>"$PLAN"; echo "plan: $PLAN"
    echo "PLAN CLEAN: $k steps, no conflicts. plan-only: no repository ref, worktree, index or object was written"
    exit 0
fi

# --- 5. apply: new branch + new worktree only. No force, no reset, no cleanup on failure.
for s in "${steps[@]}"; do
    read -r n sha <<<"$s"
    [ "${Mdisp[$n]}" = INCLUDE ] || continue
    git fetch -q --no-tags --no-write-fetch-head "$S" "refs/pmb/pr/$n" || { bad fetch "#$n: into repository"; exit 1; }
done
for s in "${steps[@]}"; do
    read -r n sha <<<"$s"
    git cat-file -e "$sha^{commit}" 2>/dev/null || { bad object "#$n: $sha not in repository"; exit 1; }
done
git worktree add -b "$APPLY" -- "$WT" "$BASE" || { bad exists "git worktree add -b $APPLY $WT refused"; exit 1; }
for s in "${steps[@]}"; do
    read -r n sha <<<"$s"
    git -C "$WT" merge --no-ff --no-edit -m "Merge #$n ($sha)" "$sha" \
        || { bad merge "#$n $sha in $WT failed: left as-is for review (no abort, no reset, no cleanup)"; exit 1; }
done
got=$(git -C "$WT" rev-parse "HEAD^{tree}")
[ "$got" = "$FINAL" ] || { bad drift "built tree $got != planned tree $FINAL (left in $WT)"; exit 1; }
echo "RESULT BUILT $APPLY $(git -C "$WT" rev-parse HEAD) tree=$got" >>"$PLAN"
echo "BUILT $APPLY at $(git -C "$WT" rev-parse HEAD) in $WT, tree matches plan"
