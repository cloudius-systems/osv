#!/usr/bin/env bash
#
# Copyright (C) 2014 Cloudius Systems, Ltd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Imports the client certificate and CA certificate to the local NSS store.
# This store is used by google-chrome, so you don't have to import the
# certificate manually into your browser.

set -e

echo " *** Generating PKCS12 client cert..."
echo " *** You will be asked for the password which will protect the client cert."
echo " *** This password will need to be entered when importing the certificate"

client_cert_name="osv-client"
ca_name="osv-CA"

NSS_DIR=sql:$HOME/.pki/nssdb

openssl pkcs12 -export -out build/client.p12 \
    -inkey build/client.key \
    -in build/client.pem \
    -certfile build/cacert.pem \
    -name $client_cert_name

echo " *** Importing client cert..."

certutil -d $NSS_DIR -D -n $client_cert_name || echo "Previous client cert not found"
pk12util -d $NSS_DIR -i build/client.p12

echo " *** Importing CA cert..."

certutil -d $NSS_DIR -D -n $ca_name || echo "Previous CA not found"
certutil -d $NSS_DIR -A -t PC -n $ca_name -i build/cacert.pem && echo "Import successful"

