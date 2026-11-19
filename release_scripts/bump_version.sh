#!/bin/bash
# Changes the version number in version.txt, commits it and tags the commit as vX.Y.Z.
# Usage: bump_version.sh [X.Y.Z] (default is to increment the patch number). Nothing is pushed.
set -e
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
versionfile="version.txt"

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "There are uncommitted changes, clean up first!"
    git status --short --untracked-files=no
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

echo "$new_version" > "$versionfile"
git add "$versionfile"
git commit -q -m "Version bump to $new_version"
git tag "v$new_version"
echo "Committed and tagged v$new_version. Push with: git push origin $branch v$new_version"
