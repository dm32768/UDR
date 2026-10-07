#!/usr/bin/env bash
#
# Build the udr .deb for Debian 13 from this tree:
#
#   ./build-deb.sh                  # on a Debian 13 build host; -> out/deb/
#   USE_SBUILD=1 ./build-deb.sh     # in an sbuild unshare chroot
#
# The package links the system's libudt, so the build needs libudt-dev
# (>= 4.13) from a repository that carries UDT 4.13; Debian 13's own is
# 4.11. The udt/ submodule is not packed. The version is the VERSION file's;
# debian/changelog is written here from it.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="$(tr -d '[:space:]' <"${SCRIPT_DIR}/VERSION")"
BUILD_ROOT="${BUILD_ROOT:-${HOME}/udr-deb-build}"
OUT_DIR="${SCRIPT_DIR}/out/deb"

die() { printf 'udr-deb: %s\n' "$*" >&2; exit 1; }

grep -q '^13' /etc/debian_version 2>/dev/null \
    || die "this builder targets Debian 13, found: $(cat /etc/debian_version 2>/dev/null || echo unknown)"
[[ -n "${VERSION}" ]] || die "no version in ${SCRIPT_DIR}/VERSION"

SRC_DIR="${BUILD_ROOT}/udr-${VERSION}"
rm -rf "${SRC_DIR}" "${BUILD_ROOT}"/udr_"${VERSION}"*
mkdir -p "${SRC_DIR}"
cp -a "${SCRIPT_DIR}"/{VERSION,Makefile,LICENSE.txt,README.md,udr.1,src,tests,debian} "${SRC_DIR}/"
cat >"${SRC_DIR}/debian/changelog" <<EOC
udr (${VERSION}) trixie; urgency=medium

  * UDR 0.9.4 for Debian 13: linked against the system libudt and OpenSSL 3,
    one Makefile, a loopback smoke test run in the build, a man page.

 -- Dmitry Musatov <dm@dgma.io>  $(date -R)
EOC

echo "==> Building"
cd "${SRC_DIR}"
if [[ -n "${USE_SBUILD:-}" ]]; then
    dpkg-source -b .
    sbuild --chroot-mode=unshare --dist "$(. /etc/os-release; echo "${VERSION_CODENAME}")" \
        --no-run-lintian --no-source --arch-any --no-arch-all \
        "../udr_${VERSION}.dsc"
else
    dpkg-buildpackage -us -uc -b
fi
mkdir -p "${OUT_DIR}"
rm -f "${OUT_DIR}"/*.deb
find "${BUILD_ROOT}" -maxdepth 2 -name "udr_${VERSION}_*.deb" -exec cp -v {} "${OUT_DIR}/" \;
ls "${OUT_DIR}/udr_${VERSION}"_*.deb >/dev/null 2>&1 || die "no udr_${VERSION} .deb under ${BUILD_ROOT}"
echo "==> Done:"
ls -l "${OUT_DIR}"
