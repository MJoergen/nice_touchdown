#!/bin/sh
# notes.sh TAG
#
# Prints the section of RELEASE_NOTES.md for TAG, such as V1.0: first
# the title of the release, "nice_touchdown V1.0", then its text, up to
# the next section. Fails, with nothing on stdout, unless the section is
# there and dated, as "## V1.0 (2026-11-20)": a release must have its
# date in the notes before it is tagged. The release workflow takes the
# text of the GitHub release from it, and doc/RELEASE.md the message of
# the tag.
set -eu
tag=${1:?notes.sh TAG}
notes=$(dirname "$0")/../RELEASE_NOTES.md

if ! grep -q "^## $tag ([0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9])\$" "$notes"; then
    echo "notes.sh: RELEASE_NOTES.md has no dated section \"## $tag (YYYY-MM-DD)\"" >&2
    exit 1
fi
echo "nice_touchdown $tag"
awk -v tag="$tag" '
    /^## / { inside = index($0, "## " tag " (") == 1; next }
    inside
' "$notes"
