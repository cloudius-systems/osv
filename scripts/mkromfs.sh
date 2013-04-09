
set +x

if [ ! -n "`which genromfs`" ]; then
    echo "genromfs binary not found." >&2
    exit 1
fi

rm -rf ${BUILDDIR}
mkdir -p ${BUILDDIR}

sed \
    -e "s#GCCBASE#${GCCBASE}#" \
    -e "s#JDKBASE#${JDKBASE}#" \
    -e "s#MISCBASE#${MISCBASE}#" \
    < ../../usr.manifest | \
    while read to from; do
        mkdir -p ${BUILDDIR}/`dirname ${to}`
        cp ${from} ${BUILDDIR}/${to}
    done

genromfs -f ${IMAGE} -d ${BUILDDIR}
