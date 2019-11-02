#!/bin/bash

PROTOCOL=${1:-'http'}
OSV_DIR=$(readlink -f $(dirname $0)/../..)
CMD="java.so -Djetty.base=/jetty/demo-base -jar /jetty/start.jar"

case $PROTOCOL in
	http)
		PYTHONPATH=$OSV_DIR/scripts $OSV_DIR/modules/httpserver-api/tests/testhttpserver-api.py --cmd "$CMD" --hypervisor $OSV_HYPERVISOR;;
	https)
		PYTHONPATH=$OSV_DIR/scripts $OSV_DIR/modules/httpserver-api/tests/testhttpserver-api.py --cmd "$CMD" --cert $OSV_DIR/modules/certs/build/client.pem --key $OSV_DIR/modules/certs/build/client.key --cacert $OSV_DIR/modules/certs/build/cacert.pem --hypervisor $OSV_HYPERVISOR;;
esac
