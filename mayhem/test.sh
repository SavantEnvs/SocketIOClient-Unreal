#!/usr/bin/env bash
#
# mayhem/test.sh — RUN the Socket.IO packet-codec known-answer tests that mayhem/build.sh built.
# Each case decodes/encodes a fixed wire payload and asserts the parsed namespace, ack id, type and
# message tree (or the exact encoded bytes); the SUMMARY line is parsed here, so a neutered or
# missing test binary produces no counts and fails.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

runner=/mayhem/selftest_packet
if [ ! -x "$runner" ]; then
  echo "missing $runner — mayhem/build.sh should have built it" >&2
  emit_ctrf "sio-packet-kat" 0 1 0
  exit 1
fi

out="$("$runner" 2>&1)"
echo "$out"

if ! summary="$(grep -E '^SUMMARY: [0-9]+ tests run; [0-9]+ failed$' <<<"$out")"; then
  echo "no SUMMARY line from $runner" >&2
  emit_ctrf "sio-packet-kat" 0 1 0
  exit 1
fi

passed="$(grep -c '^PASS ' <<<"$out")"
failed="$(grep -c '^FAIL ' <<<"$out")"
run="$(sed -E 's/^SUMMARY: ([0-9]+) tests run.*/\1/' <<<"$summary")"
if [ "$run" -lt 11 ] || [ $((passed + failed)) -ne "$run" ]; then
  echo "expected at least 11 cases and consistent counts, got run=$run passed=$passed failed=$failed" >&2
  emit_ctrf "sio-packet-kat" "$passed" $((failed + 1)) 0
  exit 1
fi
emit_ctrf "sio-packet-kat" "$passed" "$failed" 0
