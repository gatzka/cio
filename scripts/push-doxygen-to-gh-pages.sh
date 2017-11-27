#!/bin/bash

#if [ "$TRAVIS_REPO_SLUG" == "gatzka/cio" ] && [ "$CMAKE_C_COMPILER" == "gcc" ] && [ "$CMAKE_BUILD_TYPE" == "Release" ] && [ "$TRAVIS_PULL_REQUEST" == "false" ] && [ "$TRAVIS_BRANCH" == "master" ]; then
if [ "$TRAVIS_REPO_SLUG" == "gatzka/cio" ] && [ "$CREATE_DOXY" = "true" ] && [ "$TRAVIS_PULL_REQUEST" == "false" ] && [ "$TRAVIS_BRANCH" == "master" ]; then
#if [ "$TRAVIS_REPO_SLUG" == "gatzka/cio" ] && [ "$TRAVIS_PULL_REQUEST" == "false" ] && [ "$TRAVIS_BRANCH" == "master" ]; then

  echo -e "Publishing doxygen...\n"

  cp -R src/docs/doc/html/ $HOME/doxygen-latest

  cd $HOME
  git config --global user.email "travis@travis-ci.org"
  git config --global user.name "travis-ci"
  git clone --branch=gh-pages https://${GH_TOKEN}@github.com/gatzka/cio gh-pages
  #git clone --quiet --branch=gh-pages https://${GH_TOKEN}@github.com/gatzka/cio gh-pages > /dev/null

  cd gh-pages
  git rm -rf ./doc
  cp -Rf $HOME/doxygen-latest ./doc
  git add -f .
  git commit -m "Latest doxygen on successful travis build $TRAVIS_BUILD_NUMBER auto-pushed to gh-pages"
  git push -fq origin gh-pages
  #git push -fq origin gh-pages > /dev/null

  echo -e "Published doxygen to gh-pages.\n"
  
fi
