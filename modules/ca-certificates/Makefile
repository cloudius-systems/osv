DEST=build/etc/pki/ca-trust/extracted

quiet = $(if $V, $1, @echo " $2"; $1)
very-quiet = $(if $V, $1, @$1)

.PHONY: module
module:
	$(call very-quiet, mkdir -p $(DEST)/openssl)
	$(call very-quiet, mkdir -p $(DEST)/pem)
	$(call very-quiet, mkdir -p $(DEST)/java)
	$(call very-quiet, p11-kit extract --format=openssl-bundle --filter=certificates --overwrite $(DEST)/openssl/ca-bundle.trust.crt)
	$(call very-quiet, p11-kit extract --format=pem-bundle --filter=ca-anchors --overwrite --purpose server-auth $(DEST)/pem/tls-ca-bundle.pem)
	$(call very-quiet, p11-kit extract --format=pem-bundle --filter=ca-anchors --overwrite --purpose email $(DEST)/pem/email-ca-bundle.pem)
	$(call very-quiet, p11-kit extract --format=pem-bundle --filter=ca-anchors --overwrite --purpose code-signing $(DEST)/pem/objsign-ca-bundle.pem)
	$(call very-quiet, p11-kit extract --format=java-cacerts --filter=ca-anchors --overwrite --purpose server-auth $(DEST)/java/cacerts)

clean:
	rm -rf $(DEST)
