DEST=build/etc/pki/ca-trust/extracted

.PHONY: module
module:
	mkdir -p $(DEST)/openssl
	mkdir -p $(DEST)/pem
	mkdir -p $(DEST)/java
	p11-kit extract --format=openssl-bundle --filter=certificates --overwrite $(DEST)/openssl/ca-bundle.trust.crt
	p11-kit extract --format=pem-bundle --filter=ca-anchors --overwrite --purpose server-auth $(DEST)/pem/tls-ca-bundle.pem
	p11-kit extract --format=pem-bundle --filter=ca-anchors --overwrite --purpose email $(DEST)/pem/email-ca-bundle.pem
	p11-kit extract --format=pem-bundle --filter=ca-anchors --overwrite --purpose code-signing $(DEST)/pem/objsign-ca-bundle.pem
	p11-kit extract --format=java-cacerts --filter=ca-anchors --overwrite --purpose server-auth $(DEST)/java/cacerts

clean:
	rm -rf $(DEST)
