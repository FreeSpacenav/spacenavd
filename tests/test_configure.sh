#!/bin/sh
set -eu
# Called from run.sh's isolated build tree.
mkdir -p configure-test/src configure-test/bin
cp configure Makefile.in configure-test/
cat > configure-test/bin/pkg-config <<'MOCK'
#!/bin/sh
# Fail if the build asks for Cairo, even when LCD dependencies exist.
for arg do
	case "$arg" in *cairo*|*rsvg*) exit 1;; esac
done
[ "${LCD_DEPS_MISSING:-0}" = 0 ]
MOCK
chmod +x configure-test/bin/pkg-config
cd configure-test
PATH="$PWD/bin:$PATH"
export PATH
./configure --enable-spacelcd > configure.log 2>&1
grep -q '^#define HAVE_SPACELCD$' src/config.h
if LCD_DEPS_MISSING=1 ./configure --enable-spacelcd > configure.log 2>&1; then
	echo 'Explicit LCD enable must fail if dependencies are missing' >&2
	exit 1
fi
LCD_DEPS_MISSING=1 ./configure > configure.log 2>&1
if grep -q '^#define HAVE_SPACELCD$' src/config.h; then exit 1; fi
./configure --disable-spacelcd > configure.log 2>&1
if grep -q '^#define HAVE_SPACELCD$' src/config.h; then exit 1; fi
printf 'Configure tests passed\n'
