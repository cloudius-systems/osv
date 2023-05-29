#!/bin/bash

OSV_DIR=$(readlink -f $(dirname $0)/../..)
CAPSTAN_REPO=$HOME/.capstan

OSV_VERSION=$($OSV_DIR/scripts/osv-version.sh | cut -d - -f 1 | grep -Po "[^v]*")
OSV_COMMIT=$($OSV_DIR/scripts/osv-version.sh | grep -Po "\-g.*" | grep -oP "[^-g]*")

if [ "$OSV_COMMIT" != "" ]; then
  OSV_VERSION="$OSV_VERSION-$OSV_COMMIT"
fi

argv0=${0##*/}
usage() {
	cat <<-EOF
	Compose and test apps out ouf pre-built capstan packages

	Usage: ${argv0} [options] <group_of_tests> | <test_app_name>
	Options:
	  -c              Compose test app image only
	  -r              Run test app only (must have been composed earlier)
	  -R              Compose test app image with RoFS (ZFS is the default)
	  -l              Use latest OSv kernel from build/last to build test image
	  -f              Run OSv on firecracker
	  -v              Verbose: show output from capstan and tested app
	  -L loader	  Use specific loader from capstan repository

	Test groups:
	  simple - simple apps like golang-example
	  http - httpserver apps
	  http-java - java httpserver apps
	  java <name> - java app
	  http-node - node http apps
	  node <name> - node app
	  with_tester - apps tested with extra tester script like redis
	  unit_tests - unit tests
	  httpserver_api_tests - httpserver API unit tests
	  all - all apps
	EOF
	exit ${1:-0}
}

FS=zfs
COMPOSE_ONLY=false
RUN_ONLY=false
OSV_HYPERVISOR="qemu"

while getopts crRlL:fh: OPT ; do
	case ${OPT} in
	c) COMPOSE_ONLY=true;;
	r) RUN_ONLY=true;;
	R) FS=rofs;;
	l) LOADER="osv-latest-loader";;
	f) OSV_HYPERVISOR="firecracker";;
	L) LOADER="$OPTARG";;
	h) usage;;
	?) usage 1;;
	esac
done

if [ "$LOADER" == "" ]; then
	if [ "$FS" == "rofs" ]; then
		LOADER="osv-loader"
	else
		LOADER="osv-loader-with-zfs"
	fi
fi

export OSV_HYPERVISOR

shift $((OPTIND - 1))
[[ -z $1 ]] && usage 1

TEST_APP_PACKAGE_NAME="$1"
TEST_OSV_APP_NAME="$2"

determine_platform() {
  if [ -f /etc/os-release ]; then
    PLATFORM=$(grep PRETTY_NAME /etc/os-release | cut -d = -f 2 | grep -o -P "[^\"]+")
  elif [ -f /etc/lsb-release ]; then
    PLATFORM=$(grep DISTRIB_DESCRIPTION /etc/lsb-release | cut -d = -f 2 | grep -o -P "[^\"]+")
  else
    PLATFORM="Unknown Linux"
  fi
}

copy_latest_loader() {
   mkdir -p "$CAPSTAN_REPO/repository/$LOADER"
  cp -a $OSV_DIR/build/last/loader.img "$CAPSTAN_REPO/repository/$LOADER/$LOADER.qemu"
  determine_platform
  cat << EOF > $CAPSTAN_REPO/repository/$LOADER/index.yaml
format_version: "1"
version: "$OSV_VERSION"
created: $(date +'%Y-%m-%d %H:%M')
description: "OSv kernel"
platform: "$PLATFORM"
EOF
  echo "Using latest OSv kernel from $OSV_DIR/build/last/loader.img !"
}

compose_test_app()
{
  local APP_NAME=$1
  local DEPENDANT_PKG1=$2
  local DEPENDANT_PKG2=$3

  local IMAGE_PATH="$CAPSTAN_REPO/repository/test-$APP_NAME-$FS/test-$APP_NAME-$FS.qemu"

  if [ $RUN_ONLY == false ]; then
    local DEPENDENCIES="--require osv.$APP_NAME"
    if [ "$DEPENDANT_PKG1" != "" ]; then
      DEPENDENCIES="--require osv.$DEPENDANT_PKG1 $DEPENDENCIES"
    fi
    if [ "$DEPENDANT_PKG2" != "" ]; then
      DEPENDENCIES="--require osv.$DEPENDANT_PKG2 $DEPENDENCIES"
    fi

    echo "-------------------------------------------------------"
    echo " Composing $APP_NAME into $FS image at $IMAGE_PATH ... "
    echo "-------------------------------------------------------"

    if [ "$FS" == "rofs" ]; then
      FSTAB=static/etc/fstab_rofs
    else
      FSTAB=static/etc/fstab
    fi

    echo "capstan package compose $DEPENDENCIES --fs $FS --loader_image $LOADER test-$APP_NAME-$FS"
    TEMPDIR=$(mktemp -d) && pushd $TEMPDIR > /dev/null && \
      mkdir -p etc && cp $OSV_DIR/$FSTAB etc/fstab && \
      capstan package compose $DEPENDENCIES --fs $FS --loader_image $LOADER "test-$APP_NAME-$FS" && \
      rm -rf $TEMPDIR && popd > /dev/null
  else
    echo "Reusing the test image: $IMAGE_PATH that must have been composed before!"
  fi

  if [ -f "$IMAGE_PATH" ]; then
    cp $CAPSTAN_REPO/repository/"test-$APP_NAME-$FS"/"test-$APP_NAME-$FS".qemu $OSV_DIR/build/last/usr.img
  else
    echo "Could not find test image: $IMAGE_PATH!"
    exit 1
  fi
}

run_test_app()
{
  local OSV_APP_NAME=$1
  local TEST_PARAMETER=$2

  if [ $COMPOSE_ONLY == false ]; then
    echo "-------------------------------------------------------"  | tee -a $STATUS_FILE
    echo " Testing $OSV_APP_NAME ... "  | tee -a $STATUS_FILE

    local LOG_FILE=$LOG_DIR/${OSV_APP_NAME}.log
    if [ "$TEST_PARAMETER" != "" ]; then
      LOG_FILE=$LOG_DIR/${OSV_APP_NAME}_${TEST_PARAMETER}.log
    fi
    rm -f $LOG_FILE
    if [ -f $OSV_DIR/apps/$OSV_APP_NAME/test.sh ]; then
      $OSV_DIR/apps/$OSV_APP_NAME/test.sh $TEST_PARAMETER > >(tee -a $LOG_FILE) 2> >(tee -a $LOG_FILE >&2)
    elif [ -f $OSV_DIR/modules/$OSV_APP_NAME/test.sh ]; then
      $OSV_DIR/modules/$OSV_APP_NAME/test.sh $TEST_PARAMETER > >(tee -a $LOG_FILE) 2> >(tee -a $LOG_FILE >&2)
    fi

    echo "-------------------------------------------------------"  | tee -a $STATUS_FILE
  fi
  echo ''
}

compose_and_run_test_app()
{
  local APP_NAME=$1
  compose_test_app $APP_NAME
  run_test_app $APP_NAME
}

# Simple stateless apps that should work with both ZFS and ROFS
test_simple_apps() #stateless
{
  compose_and_run_test_app "golang-example"
  compose_and_run_test_app "golang-pie-example"
  compose_and_run_test_app "graalvm-example"
  compose_and_run_test_app "lua-hello-from-host"
  compose_and_run_test_app "rust-example"
  compose_and_run_test_app "stream"
  compose_test_app "python2-from-host" && run_test_app "python-from-host" 2
  compose_test_app "python3-from-host" && run_test_app "python-from-host" 3
}

test_java_app()
{
  compose_test_app "$1" "run-java" "openjdk8-zulu-compact3-with-java-beans" && run_test_app "$1"
}

# Stateless http java apps that should work with both ZFS and ROFS
test_http_java_apps()
{
  #TODO: Test with multiple versions of java
  test_java_app "jetty"
  test_java_app "tomcat"
  test_java_app "vertx"
  test_java_app "spring-boot-example" #Really slow
}

test_node_app()
{
  compose_test_app "$1" "node-from-host" && run_test_app "$1"
}

# Stateless node apps that should work with both ZFS and ROFS
test_http_node_apps()
{
  #TODO: Test with multiple versions of node
  test_node_app "node-express-example"
  test_node_app "node-socketio-example"
}

# Stateless http apps that should work with both ZFS and ROFS except for nginx
test_http_apps() #stateless
{
  compose_and_run_test_app "golang-httpserver"
  compose_and_run_test_app "golang-pie-httpserver"
  compose_and_run_test_app "graalvm-httpserver"
  compose_and_run_test_app "lighttpd"
  compose_test_app "nginx" && run_test_app "nginx-from-host"
  compose_and_run_test_app "rust-httpserver"
  test_http_java_apps
  test_http_node_apps
}

test_ffmpeg()
{
  if [ "$OSV_HYPERVISOR" == "firecracker" ]; then
    echo "Skipping ffmpeg as at this time it cannot run on Firecracker. Needs to change this script to setup bridge parameter"
  else
    compose_test_app "ffmpeg" && run_test_app "ffmpeg" "video_subclip" && run_test_app "ffmpeg" "video_transcode"
  fi
}

test_redis()
{
  compose_and_run_test_app "redis-memonly" && run_test_app "redis-memonly" "ycsb"
}

test_keydb()
{
  compose_and_run_test_app "keydb" && run_test_app "keydb" "ycsb"
}

test_apps_with_tester() #most stateless
{
  compose_and_run_test_app "iperf3"
  compose_and_run_test_app "graalvm-netty-plot"
  test_ffmpeg
  test_redis
  test_keydb
  compose_and_run_test_app "cli"
  if [ "$FS" == "zfs" ]; then #These are stateful apps
    compose_and_run_test_app "mysql"
    test_java_app "apache-derby"
    test_java_app "apache-kafka"
    compose_and_run_test_app "elasticsearch"
  fi
}

run_unit_tests() #regular unit tests are stateful
{
  # Unit tests are special as the unit tests runner depends on usr.manifest which
  # needs to be placed in the tests module. So let us gegnerate it on the fly from the unit tests mpm
  capstan package describe -c osv.common-tests | grep "/tests/tst-" | grep -o "/tests/tst-.*" | sed 's/$/: dummy/' > $OSV_DIR/modules/tests/usr.manifest
  capstan package describe -c "osv.$FS-tests" | grep "/tests/tst-" | grep -o "/tests/tst-.*" | sed 's/$/: dummy/' >> $OSV_DIR/modules/tests/usr.manifest
  compose_test_app "$FS-tests" "openjdk8-from-host" "common-tests" && run_test_app "tests"
}

run_httpserver_api_tests()
{
  if [ "$FS" == "zfs" ]; then #These are stateful apps
    compose_test_app "httpserver-api-tests" && run_test_app "httpserver-api" "http"
  fi
}

export LOG_DIR=/tmp/osv_tests
mkdir -p $LOG_DIR
export STATUS_FILE="$LOG_DIR/$TEST_APP_PACKAGE_NAME.status"
rm -f $STATUS_FILE

#Copy latest OSv kernel if requested by user
if [ "$LOADER" == "osv-latest-loader" ] && [ $RUN_ONLY == false ]; then
  copy_latest_loader
fi

case "$TEST_APP_PACKAGE_NAME" in
  simple)
    echo "Testing simple apps ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    test_simple_apps;;
  http)
    echo "Testing HTTP apps ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    test_http_apps;;
  http-java)
    echo "Testing HTTP Java apps ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    test_http_java_apps;;
  java)
    echo "Testing Java app ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    test_java_app $TEST_OSV_APP_NAME;;
  http-node)
    echo "Testing HTTP Node apps ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    test_http_node_apps;;
  node)
    echo "Testing Node app ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    test_node_app $TEST_OSV_APP_NAME;;
  with_tester)
    echo "Testing apps with custom tester ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    test_apps_with_tester;;
  unit_tests)
    echo "Running unit tests ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    run_unit_tests;;
  redis)
    echo "Running redis test..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    test_redis;;
  keydb)
    echo "Running keydb test..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    test_keydb;;
  ffmpeg)
    echo "Running ffmpeg test..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    test_ffmpeg;;
  httpserver_api_tests)
    echo "Running httpserver api tests ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    run_httpserver_api_tests;;
  all)
    echo "Running all tests ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    run_unit_tests
    run_httpserver_api_tests
    test_simple_apps
    test_http_apps
    compose_and_run_test_app specjvm
    test_apps_with_tester;;
  *)
    echo "Running $TEST_APP_PACKAGE_NAME ..." | tee -a $STATUS_FILE
    echo "-----------------------------------" | tee -a $STATUS_FILE
    if [ "$TEST_OSV_APP_NAME" == "" ]; then
      compose_and_run_test_app "$TEST_APP_PACKAGE_NAME"
    else
      compose_test_app "$TEST_APP_PACKAGE_NAME" && run_test_app "$TEST_OSV_APP_NAME"
    fi
esac

#
#TODO
#osv.netperf
#osv.memcached
#
#-- Think of Ruby
