#!/usr/bin/env bash
#
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Verify the inventory file against LIVE GitHub. The inventory is a CACHE and it has
# already gone stale within HOURS (#1517 was open while the file had zero mentions of it, and the
# declared totals said 65/60 when reality was 66/61).
#
# WHY THIS EXISTS: the bench gate caught that drift only because it derives its PR set from GitHub.
# Nothing was checking the inventory itself, so the file could disagree with reality indefinitely -
# and it is the file humans read. A cache with no invalidation is a stale claim waiting to be quoted.
#
# Usage:
#   check-inventory.sh [INVENTORY]                       live GitHub (GET only), default inventory
#   check-inventory.sh [--inventory FILE] --live-json PAGES.json --live-total TOTAL.json
#       PAGES.json = output of: gh api --paginate --slurp 'repos/R/pulls?state=open&per_page=50&...'
#       TOTAL.json = output of: gh api 'search/issues?q=repo:R+is:pr+is:open&per_page=1'
#   Exit 0 = in sync, 1 = inventory stale, 2 = could not evaluate (bad input / query failure / usage).
# Negative oracle (must stay green): the negative test suite
#
# What counts as a row: an indented line `  #N <title> ... `<40-hex head>`` [ (login[, draft]) ] inside
# one of the three PR sections. Section header gives group and draft state:
#   ## OUR <n> NON-DRAFT PRs   ## OUR <n> DRAFT PRs   ## <n> THIRD-PARTY PRs  (rows need `(login)`,
#   `(login, draft)` for a third-party draft). Any other heading ends the section. Prose mentions,
#   column-0 `#N`, and row-shaped lines outside the PR sections are NOT rows and never satisfy a match.
# Every open PR (ALL authors) must have exactly one row with the same number, head SHA, draft flag,
# author group and login; every row must be an open PR; section and summary counts must equal live.
# "Non-draft" is GitHub draft state only. It is NOT readiness and this script never claims it is.
#
# History (TOOL-4/5 repair, 2026-10-07): the prior version used `gh pr list --limit 100` (a cap, not
# pagination), checked only our rows, let a whitespace-delimited prose `#N` satisfy a "match", never
# compared head/draft/group, demanded a "$ready ready" phrase (ready = ours - draft: a false readiness
# claim), shared /tmp/inv-live.txt, and its only "negative test" grepped for #99999, which exercised none
# of the real checks. Earlier lessons kept: never `grep -q` at the end of a pipe under pipefail (SIGPIPE
# made the gate flaky: 4/200 false STALE); inputs are read once into variables, no early-exit pipes.
#
# Copyright (C) 2026 Greg Burd
set -uo pipefail
export LC_ALL=C
R=
OURS=gburd
PER_PAGE=50
INV=
LIVE_JSON='' LIVE_TOTAL=''

usage() { echo "usage: $0 [INVENTORY] | [--inventory FILE] [--repo OWNER/NAME] [--live-json FILE --live-total FILE]" >&2; exit 2; }
while [ $# -gt 0 ]; do
    case $1 in
        --repo)       [ $# -ge 2 ] || usage; R=$2; shift 2 ;;
        --inventory)  [ $# -ge 2 ] || usage; INV=$2; shift 2 ;;
        --live-json)  [ $# -ge 2 ] || usage; LIVE_JSON=$2; shift 2 ;;
        --live-total) [ $# -ge 2 ] || usage; LIVE_TOTAL=$2; shift 2 ;;
        -*) usage ;;
        *) INV=$1; shift ;;
    esac
done
# Fixture mode needs BOTH files: without the independent total a truncated page 2 cannot be detected.
if [ -n "$LIVE_JSON" ] || [ -n "$LIVE_TOTAL" ]; then
    [ -n "$LIVE_JSON" ] && [ -n "$LIVE_TOTAL" ] || { echo "FAIL[usage]: --live-json and --live-total go together"; exit 2; }
fi
[ -f "$INV" ] || { echo "FAIL[inventory]: no inventory at $INV"; exit 2; }

W=$(mktemp -d "${TMPDIR:-/tmp}/check-inventory.XXXXXX") || { echo "FAIL: mktemp -d"; exit 2; }
trap 'rm -rf "$W"' EXIT

# --- live ground truth: ALL AUTHORS, COMPLETE pagination (filtering by --author once hid the
# maintainer's own 5 open PRs for weeks, including #1390), plus an independent count to catch truncation.
if [ -z "$LIVE_JSON" ]; then
    [ -n "$R" ] || { echo "FAIL[usage]: --repo OWNER/NAME required for a live query"; exit 2; }
    if ! gh api --paginate --slurp "repos/$R/pulls?state=open&per_page=$PER_PAGE&sort=created&direction=asc" \
            >"$W/live.json" 2>"$W/gh.err" \
       || ! gh api "search/issues?q=repo:$R+is:pr+is:open&per_page=1" >"$W/total.json" 2>>"$W/gh.err"; then
        echo "FAIL[gh]: could not query GitHub (auth? network?) - NOT reporting clean"; head -5 "$W/gh.err"; exit 2
    fi
    LIVE_JSON=$W/live.json LIVE_TOTAL=$W/total.json
fi

# Validate shape and extract "number login draft sha". -s + length==1 rejects empty and multi-document input.
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
    | .[] | "\(.number) \(.user.login) \(.draft) \(.head.sha)"' "$LIVE_JSON" 2>"$W/jq.err"); then
    echo "FAIL[json]: live PR list unusable: $(head -c 300 "$W/jq.err") - NOT reporting clean"; exit 2
fi
pages=$(jq -r 'map(length) | map(tostring) | join("+")' "$LIVE_JSON")
if ! total=$(jq -rs 'if length == 1 and (.[0].total_count | type) == "number" and .[0].incomplete_results == false
                     then .[0].total_count else error("bad search total") end' "$LIVE_TOTAL" 2>"$W/jq.err"); then
    echo "FAIL[json]: independent total unusable: $(head -c 300 "$W/jq.err") - NOT reporting clean"; exit 2
fi

declare -A Llogin Ldraft Lsha
n=0 ours=0 draft=0
while read -r num login dr sha; do
    [ -n "$num" ] || continue
    Llogin[$num]=$login Ldraft[$num]=$dr Lsha[$num]=$sha
    n=$((n + 1))
    if [ "$login" = "$OURS" ]; then ours=$((ours + 1)); [ "$dr" = true ] && draft=$((draft + 1)); fi
done <<<"$live"
nondraft=$((ours - draft)) third=$((n - ours))
# POSITIVE CONTROL: an empty or tiny result means the query failed, not that the repo emptied.
if [ "$n" -lt 10 ]; then
    echo "FAIL[implausible]: live list has only $n PRs - treating as query failure"; exit 2
fi
if [ "$total" != "$n" ]; then
    echo "FAIL[total]: list has $n PRs (pages $pages) but independent search total_count is $total -" \
         "truncated or raced pagination; NOT reporting clean (retry)"; exit 2
fi
echo "LIVE: $n open (pages $pages; independent total $total) = $ours ours ($nondraft non-draft + $draft draft) + $third third-party"
echo "      non-draft = GitHub draft state only, not readiness"

# --- parse inventory rows once (gawk: match() with capture array)
# REVIEW LOW: text inside <!-- ... --> (across lines; unclosed = to EOF) is invisible when rendered: strip it first
inv=$(gawk -v ours="$OURS" '
    {   out = ""; t = $0
        while (t != "") {
            if (incom) { i = index(t, "-->"); if (!i) break; t = substr(t, i + 3); incom = 0 }
            else { i = index(t, "<!--"); if (!i) { out = out t; break }; out = out substr(t, 1, i - 1); t = substr(t, i + 4); incom = 1 }
        }
        sub(/[[:space:]]+$/, "", out); $0 = out
    }
    /^#+[[:space:]]/ {
        sec = ""
        if (match($0, /^## OUR ([0-9]+) NON-DRAFT PRs/, m))      sec = "ours-nondraft"
        else if (match($0, /^## OUR ([0-9]+) DRAFT PRs/, m))     sec = "ours-draft"
        else if (match($0, /^## ([0-9]+) THIRD-PARTY PRs/, m))   sec = "third"
        if (sec != "") { if (sec in decl) print "DUPSECTION", sec, NR; decl[sec] = m[1] }
        next
    }
    sec != "" && /^[[:space:]]+#[0-9]/ {
        if (!match($0, /^[[:space:]]+#([0-9]+)[[:space:]].*`([0-9a-f]{40})`([[:space:]]+\(([^(),[:space:]]+)(, draft)?\))?[[:space:]]*$/, m)) {
            print "MALFORMED", NR; next
        }
        login = m[4]; dr = (sec == "ours-draft") ? "true" : "false"
        if (sec == "third") { if (login == "") { print "MALFORMED", NR; next } if (m[5] != "") dr = "true" }
        else { if (m[5] != "") { print "MALFORMED", NR; next } if (login == "") login = ours }
        print "ROW", m[1], login, dr, m[2], (sec == "third" ? "third" : "ours"), NR
    }
    END { for (s in decl) print "DECL", s, decl[s] }' "$INV") || { echo "FAIL[inventory]: could not parse $INV"; exit 2; }

fail=0
bad() { echo "FAIL[$1]: ${*:2}"; fail=1; }
declare -A Ilogin Idraft Isha Igrp Iline Decl
while read -r kind a b c d e f; do
    case $kind in
        DUPSECTION) bad section "section '$a' header appears twice (line $b)" ;;
        MALFORMED)  bad malformed "line $a is inside a PR section and starts like a row but is not one" ;;
        DECL)       Decl[$a]=$b ;;
        ROW)
            if [ -n "${Iline[$a]:-}" ]; then bad dup "#$a listed twice (lines ${Iline[$a]} and $f)"; continue; fi
            Ilogin[$a]=$b Idraft[$a]=$c Isha[$a]=$d Igrp[$a]=$e Iline[$a]=$f ;;
    esac
done <<<"$inv"

# --- 1. every open PR (all authors) has exactly one row with identical number/head/draft/group/login
for num in $(printf '%s\n' "${!Llogin[@]}" | sort -n); do
    lg=third; [ "${Llogin[$num]}" = "$OURS" ] && lg=ours
    if [ -z "${Iline[$num]:-}" ]; then
        bad missing "#$num (${Llogin[$num]}, draft=${Ldraft[$num]}) is open but has no row in a PR section"; continue
    fi
    [ "${Isha[$num]}" = "${Lsha[$num]}" ] || bad head "#$num line ${Iline[$num]}: inventory ${Isha[$num]} != live ${Lsha[$num]}"
    [ "${Idraft[$num]}" = "${Ldraft[$num]}" ] || bad draft "#$num line ${Iline[$num]}: inventory draft=${Idraft[$num]} != live draft=${Ldraft[$num]}"
    [ "${Igrp[$num]}" = "$lg" ] || bad group "#$num line ${Iline[$num]}: listed as ${Igrp[$num]} but author ${Llogin[$num]} is $lg"
    [ "${Ilogin[$num]}" = "${Llogin[$num]}" ] || bad author "#$num line ${Iline[$num]}: inventory author ${Ilogin[$num]} != live ${Llogin[$num]}"
done
# --- 2. every row is an open PR
for num in $(printf '%s\n' "${!Iline[@]}" | sort -n); do
    [ -n "${Llogin[$num]:-}" ] || bad closed "#$num listed at line ${Iline[$num]} but is not open on GitHub"
done
# --- 3. declared counts equal live counts (section headers and the one summary line)
for kv in "ours-nondraft $nondraft" "ours-draft $draft" "third $third"; do
    set -- $kv
    [ "${Decl[$1]:-MISSING}" = "$2" ] || bad section "header for '$1' declares ${Decl[$1]:-MISSING}, live is $2"
done
want="**$n open PRs total = $ours OURS ($nondraft non-draft + $draft draft) + $third third-party.**"
summ=$(grep -E 'open PRs total' "$INV")
if [ "$(grep -cE 'open PRs total' "$INV")" != 1 ] || [ "$summ" != "$want" ]; then
    bad totals "expected exactly one summary line '$want'; found: ${summ:-none}"
fi

if [ $fail -eq 0 ]; then
    echo "PASS: all $n open PRs listed exactly once with matching head/draft/group/author; counts match"
    echo "INVENTORY IN SYNC"
else
    echo "INVENTORY STALE - fix it, do not quote it"
fi
exit $fail
