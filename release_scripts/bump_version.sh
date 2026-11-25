#!/bin/bash
# Changes the version number in version.txt, moves the [Unreleased] changes of CHANGELOG.md under the new version,
# commits them and tags the commit as vX.Y.Z.
# Usage: bump_version.sh [X.Y.Z] (default is to increment the patch number). Nothing is pushed.
set -e
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
versionfile="version.txt"
changelog="CHANGELOG.md"

# Untracked files count as well, ignored ones do not
if [ -n "$(git status --porcelain)" ]; then
    echo "The repository is not clean, commit or remove these first!"
    git status --short
    exit 1
fi
branch=$(git rev-parse --abbrev-ref HEAD)
if [ "$branch" != "main" ]; then
    echo "Releases are made from main, not from $branch!"
    exit 1
fi

current_version=$(tr -d '[:space:]' < "$versionfile")
new_version=$(echo "$current_version" | awk -F '.' '{printf("%i.%i.%i\n", $1, $2, $3+1)}')
echo "Current version is $current_version"
if [ -n "$1" ]; then
    new_version="$1"
else
    read -r -p "New version (^C to cancel, empty for $new_version): " version_given
    if [ -n "$version_given" ]; then
        new_version="$version_given"
    fi
fi
if ! [[ "$new_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Version $new_version is not of the form X.Y.Z!"
    exit 1
fi
if git rev-parse -q --verify "refs/tags/v$new_version" > /dev/null; then
    echo "Tag v$new_version already exists!"
    exit 1
fi

# The [Unreleased] changes go under a heading of the new version, and the comparison links follow
if ! grep -q '^## \[Unreleased\]$' "$changelog"; then
    echo "$changelog has no [Unreleased] section!"
    exit 1
fi
unreleased=$(awk '/^## \[Unreleased\]$/ {found = 1; next} found && /^## \[/ {exit} found && NF {print}' "$changelog")
if [ -z "$unreleased" ]; then
    echo "The [Unreleased] section of $changelog is empty, add the changes of $new_version first!"
    exit 1
fi
repository="https://github.com/JYU-IBA/jara"
awk -v version="$new_version" -v previous="$current_version" -v date="$(date +%Y-%m-%d)" -v repository="$repository" '
    /^## \[Unreleased\]$/ {print; print ""; print "## [" version "] - " date; next}
    /^\[Unreleased\]: / {
        print "[Unreleased]: " repository "/compare/v" version "...HEAD"
        print "[" version "]: " repository "/compare/v" previous "...v" version
        next
    }
    {print}
' "$changelog" > "$changelog.new"
mv "$changelog.new" "$changelog"

echo "$new_version" > "$versionfile"
git add "$versionfile" "$changelog"
git commit -q -m "Version bump to $new_version"
git tag "v$new_version"
echo "Committed and tagged v$new_version. Push with: git push origin $branch v$new_version"
