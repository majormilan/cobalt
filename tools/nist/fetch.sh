#!/bin/bash
# Download the NIST COBOL-85 test suite (CCVS85) into obj/nist/.
# The suite is not kept in the repository; it is fetched from the GnuCOBOL
# project's mirror on SourceForge (NIST no longer hosts it).

set -e
DEST=obj/nist
URL=https://sourceforge.net/projects/gnucobol/files/nist/newcob.val.tar.gz/download

mkdir -p "$DEST"
if [ ! -f "$DEST/newcob.val" ]; then
    curl -sL --max-time 300 -o "$DEST/newcob.val.tar.gz" "$URL"
    tar xzf "$DEST/newcob.val.tar.gz" -C "$DEST"
fi
echo "NIST suite: $DEST/newcob.val"
