#!/bin/sh
# Copies a tree through udr and rsync on loopback, plain and with each cipher,
# and compares the result. There is no sshd: a stand-in ssh drops its options
# and runs the remote command on this host. Then a peer without the secret
# connects to a receiver and must be refused before rsync runs.
# Usage: tests/smoke.sh ./udr
set -eu

udr=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/bin" "$work/src/sub"
cat >"$work/bin/ssh" <<'EOS'
#!/bin/sh
while [ $# -gt 0 ]; do
    case "$1" in
        -p|-l) shift 2 ;;
        -*) shift ;;
        *) break ;;
    esac
done
shift
exec sh -c "$*"
EOS
chmod 755 "$work/bin/ssh"

head -c 3000000 /dev/urandom >"$work/src/random.bin"
printf 'hello\n' >"$work/src/sub/a.txt"
: >"$work/src/empty"

cd "$work"
port=19400
for mode in plain aes-128 aes-192 aes-256 des-ede3 bf; do
    dst="$work/dst-$mode"
    mkdir -p "$dst"
    opt=""
    [ "$mode" = plain ] || opt="-n$mode"
    port=$((port + 10))
    status=0
    PATH="$work/bin:$PATH" "$udr" $opt -c "$udr" -a "$port" -b "$((port + 5))" \
        rsync -a "$work/src/" "localhost:$dst/" >"$work/out-$mode" 2>&1 || status=$?
    if [ "$status" -ne 0 ] || ! diff -r "$work/src" "$dst" >/dev/null; then
        echo "smoke: $mode FAILED (exit $status)"
        cat "$work/out-$mode"
        exit 1
    fi
    echo "smoke: $mode ok"
done

# A receiver on its own, as ssh would start it. It prints its port and secret,
# then waits for one connection.
port=$((port + 10))
"$udr" -a "$port" -b "$port" -t rsync >"$work/recv.out" 2>"$work/recv.err" &
rpid=$!
i=0
while [ ! -s "$work/recv.out" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
rport=$(cut -d' ' -f1 "$work/recv.out")
mkdir -p "$work/dst-auth"
wrong=$(printf '%064d' 0)
UDR_KEY="$wrong" timeout 20 "$udr" -s "$rport" localhost rsync --server -a . "$work/dst-auth/" \
    </dev/null >/dev/null 2>&1 || true
status=0
wait $rpid || status=$?
if [ "$status" -eq 0 ] || ! grep -q "failed authentication" "$work/recv.err"; then
    echo "smoke: auth FAILED (receiver exit $status)"
    cat "$work/recv.err"
    exit 1
fi
echo "smoke: auth ok (receiver refused a peer without the secret)"
