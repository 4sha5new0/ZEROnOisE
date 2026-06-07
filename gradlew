#!/bin/sh
# Gradle start up script for POSIX shell (Gradle 8.x official template)

APP_NAME="Gradle"
APP_BASE_NAME=$(basename "$0")
DEFAULT_JVM_OPTS="-Xmx64m -Xms64m"

# Resolve APP_HOME
PRG="$0"
while [ -h "$PRG" ]; do
  ls=$(ls -ld "$PRG")
  link=$(expr "$ls" : '.*-> \(.*\)$')
  if expr "$link" : '/.*' > /dev/null; then PRG="$link"
  else PRG=$(dirname "$PRG")/"$link"
  fi
done
SAVED=$(pwd)
cd "$(dirname "$PRG")/" > /dev/null
APP_HOME=$(pwd -P)
cd "$SAVED" > /dev/null

CLASSPATH="$APP_HOME/gradle/wrapper/gradle-wrapper.jar"

# Locate java
if [ -n "$JAVA_HOME" ]; then
  if [ -x "$JAVA_HOME/jre/sh/java" ]; then
    JAVACMD="$JAVA_HOME/jre/sh/java"
  else
    JAVACMD="$JAVA_HOME/bin/java"
  fi
  [ ! -x "$JAVACMD" ] && { echo "JAVA_HOME is set to an invalid directory: $JAVA_HOME"; exit 1; }
else
  JAVACMD="java"
  command -v java > /dev/null 2>&1 || { echo "java not found in PATH."; exit 1; }
fi

# Build JVM args (no embedded quotes)
exec "$JAVACMD" \
  $DEFAULT_JVM_OPTS \
  $JAVA_OPTS \
  $GRADLE_OPTS \
  -classpath "$CLASSPATH" \
  "-Dorg.gradle.appname=$APP_BASE_NAME" \
  org.gradle.wrapper.GradleWrapperMain \
  "$@"
