#!/bin/sh
#
# This script parses the commit messages between the last 2 git tags
# if the last commit contains the word "Release", otherwise it shows
# all the changes between the last tag and HEAD.
#
# Commits are grouped in different sections specified in the commit
# message with the middle-dot character '·'. The section names are
# arbitrary and we may be careful to use them properly but having
# in mind that this may change.
#
# Commits without any middle·dot in the message are discarted and
# displayed in the "to review" section.
#
# The tool prints Markdown, no plans to support other formats.
#
# Usage: sys/release-notes.sh 4.5.1      # from HEAD to 4.5.1
#   $ sys/release-notes.sh 4.5.1 -v      # same as above but include untagged commits
#   $ sys/release-notes.sh 4.5.0 4.5.1   # from 4.5.0 to 4.5.1
#
# --pancake

if [ -n "`git log -n 1 | grep Release`" ]; then
	VERS=`git tag --sort=committerdate | grep -v conti | tail -n 1`
	PREV=`git tag --sort=committerdate | grep -v conti | tail -n 2 | head -n1`
else
	VERS=HEAD
	PREV=`git tag --sort=committerdate | grep -v conti | tail -n 1`
fi

[ -n "$1" ] && PREV="$1"
[ -n "$2" ] && VERS="$2"

git log ${PREV}..${VERS} > .l
# When HEAD contains a tag do this magic
if [ ! -s .l ]; then
  VERS=$PREV
  PREV=`git tag --sort=committerdate | grep -v conti | tail -n 2 | head -n1`
  git log ${PREV}..${VERS} > .l
fi
grep ^Author .l | cut -d : -f 2- | sed -e 's,radare,pancake,' | sort -u > .A

echo "## Release Notes"
echo
echo "Version: ${VERS} (previous: ${PREV})"
N=`grep ^commit .l | wc -l`
echo "Commits: $N from `wc -l .A | awk '{print $1}'` contributors"
echo
echo "Release builds (binary assets below) are considered work-in-progress, use flatpak or your distro packaging if anything is not working"
echo
echo "## Flatpak (WSL / Linux)"
echo
echo "\`\`\`sh"
echo "sudo flatpak remote-add flathub https://dl.flathub.org/repo/flathub.flatpakrepo"
echo "sudo flatpak install flathub org.radare.iaito"
echo "export QT_QPA_PLATFORM=wayland   # only mandatory on windows"
echo "flatpak run org.radare.iaito"
echo "\`\`\`"
echo
echo "## Build from Source"
echo
echo "\`\`\`sh"
echo "curl -sL https://github.com/radareorg/iaito/releases/download/${VERS}/${VERS}.tar.gz | tar xzv"
echo "iaito-${VERS}/sys/install.sh"
echo "\`\`\`"
echo
echo "## Highlights"

echo "<details><summary>More details</summary><p>"
echo
echo "## Authors"
echo
cat .A | perl -ne '/([^<]+)(.*)$/;$a=$1;$b=$2;$a=~s/^\s+|\s+$//g;$b=~s/[<>\s]//g;print "[$a](mailto:$b) "'
echo
echo

cat .x | grep -v '##' | sed -e 's,^ *,,g' | grep -v "^$" | \
      perl -ne 'if (/^\*/) { print "$_"; } else { print "* $_";}'

# echo "## Changes"
# echo
# cat .l | grep -v ^commit | grep -v ^Author | grep -v ^Date > .x
# cat .x | grep '##' | perl -ne '/##([^ ]*)/; if ($1) {print "$1\n";}' | sort -u > .y
# for a in `cat .y` ; do
# 	echo "**$a**"
# 	echo
# 	cat .x | grep "##$a" | sed -e 's/##.*//g' | perl -ne '{ s/^\s+//; print "* $_"; }'
# 	echo
# done
# rm -f .x .y .l .A

echo '</p></details>'
