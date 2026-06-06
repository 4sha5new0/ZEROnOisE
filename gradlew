#!/bin/sh
#
# Gradle startup script for POSIX shell
#

# Attempt to set APP_HOME
PRG="$0"
while [ -h "$PRG" ] ; do
    ls=$(ls -ld "$PRG")
    link=$(expr "$ls" : '.*-> \(.*\)$')
    if expr "$link" : '/.*' > /dev/null; then
        PRG="$link"
    else
        PRG=$(dirname "$PRG")"/$link"
    fi
done
SAVED="$(pwd)"
cd "$(dirname "$PRG")/" >/dev/null
APP_HOME="$(pwd -P)"
cd "$SAVED" >/dev/null

APP_NAME="Gradle"
APP_BASE_NAME=$(basename "$0")

MAX_FD="maximum"
warn() { echo "$*"; }
die() { echo; echo "$*"; echo; exit 1; }

# OS specific support
cygwin=false; msys=false; darwin=false; nonstop=false
case "$(uname)" in
  CYGWIN*)  cygwin=true  ;;
  Darwin*)  darwin=true  ;;
  MSYS* | MINGW*) msys=true ;;
  NONSTOP*) nonstop=true ;;
esac

CLASSPATH=$APP_HOME/gradle/wrapper/gradle-wrapper.jar

JAVA_HOME="${JAVA_HOME:-}"
if [ -n "$JAVA_HOME" ] ; then
    if [ -x "$JAVA_HOME/jre/sh/java" ] ; then
        JAVACMD="$JAVA_HOME/jre/sh/java"
    else
        JAVACMD="$JAVA_HOME/bin/java"
    fi
    if [ ! -x "$JAVACMD" ] ; then
        die "JAVA_HOME is set to an invalid directory: $JAVA_HOME"
    fi
else
    JAVACMD="java"
    which java >/dev/null 2>&1 || die "ERROR: JAVA_HOME is not set and no 'java' found in PATH."
fi

# Increase max FD limit
if [ "$cygwin" = "false" ] && [ "$darwin" = "false" ] && [ "$nonstop" = "false" ]; then
    MAX_FD_LIMIT=$(ulimit -H -n) || warn "Could not query max file descriptor limit"
    case $MAX_FD_LIMIT in
        '' | soft) MAX_FD="$MAX_FD_LIMIT" ;;
        *) MAX_FD="$MAX_FD_LIMIT" ;;
    esac
    ulimit -n "$MAX_FD" || warn "Could not set max file descriptor limit"
fi

# JVM options
DEFAULT_JVM_OPTS='"-Xmx64m" "-Xms64m"'
GRADLE_OPTS="${GRADLE_OPTS:-}"

set -- \
    -classpath "$CLASSPATH" \
    org.gradle.wrapper.GradleWrapperMain \
    "$@"

exec "$JAVACMD" $DEFAULT_JVM_OPTS $JAVA_OPTS $GRADLE_OPTS \
    -Dorg.gradle.appname="$APP_BASE_NAME" \
    "$@"
