#!/bin/sh
#########
#### Push new translation files to iaito-translations
#### so Crowdin can fetch them

log() {
    echo "[TRANSLATIONS] $1"
}

log "Script started"

# Update submodule
log "Updating translations..."
cd ${TRAVIS_BUILD_DIR}/src
[ -d translations ] || git clone git clone https://github.com/radareorg/iaito-translations.git translations
cd translations
git pull origin master

# Generate Crowdin single translation file from iaito_ca.ts
log "Generating single translation file"
lupdate ../Iaito.pro
cp ./ca/iaito_ca.ts ./Translations.ts

# Push it so Crowdin can find new strings, and later push updated translations
log "Committing..."
git add Translations.ts
git commit -m "Updated translations"
log "Pushing..."
export GIT_SSH_COMMAND="/usr/bin/ssh -i $TRAVIS_BUILD_DIR/scripts/deploy_translations_rsa"
git push "git@github.com:radareorg/iaito-translations.git" HEAD:refs/heads/master

log "Script done!"
