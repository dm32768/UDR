# UDR

UDR runs rsync with its data channel on UDT, a reliable transport on UDP, in
place of ssh. ssh logs in and starts `udr` on the far host, and the transfer
then goes over one UDP port. This tree is the University of Chicago's UDR 0.9.4
through the jwagnerhki fork, which has `-r`, `-i`, `-d` and `-P`. It is built
for Debian 13 against the system's OpenSSL and `libudt-dev` 4.13.

```sh
git clone --recursive https://github.com/dm32768/UDR.git
make && make check       # ./udr, then a loopback transfer through each cipher
make SYSTEM_UDT=1        # link the system's libudt (libudt-dev 4.13) instead
./build-deb.sh           # the .deb, in out/deb/ (Debian 13 host)
```

UDT is the git submodule `udt/`, our fork of dorkbox/UDT, pinned to the
commit this tree is tested with. `make` builds it and links it statically;
`SYSTEM_UDT=1` uses the library installed on the host, which is what the
Debian package does. OpenSSL (`libssl-dev`) and `pkg-config` are needed
either way.

`udr` and rsync must be installed on both hosts.

## Use

```sh
udr [udr options] rsync [rsync options] source destination
udr rsync -av --progress /data/ host:/data/
udr -P 27522 -a 9000 -b 9000 rsync -a big.tar user@host:/srv/
```

| Option | Meaning |
|---|---|
| `-P port` | the remote's ssh port (22) |
| `-a port`, `-b port` | UDP port range on the receiving host (9000 to 9100) |
| `-c path` | `udr` on the remote host (`udr`, from its PATH) |
| `-d seconds` | data transfer timeout (15) |
| `-r Mbps` | cap the sending rate |
| `-m bytes` | UDT packet size, as the IP packet (1500); inside a tunnel, the tunnel's MTU |
| `-i address` | address the receiver binds to |
| `-n<cipher>` | encrypt the data channel: aes-128, aes-192, aes-256, des-ede3 (below) |
| `-v` | verbose |

`-e` and `--rsh` belong to udr. The man page is `udr(1)`. Both hosts need this
version or later: the remote udr makes a random secret for each transfer,
reports it over ssh with the port, and serves only the peer that presents it
first on the UDP port.

## Firewall

The receiving host takes inbound UDP on one port per transfer running at the same
time, from the `-a` to `-b` range. The sending host needs nothing beyond replies to
what it started. `-a 9000 -b 9000` is one port and one transfer at a time.

## Encryption

`-n` encrypts the data channel, and the result is not good enough for data that
matters.

- Both directions use one key, with an all-zero IV, in CFB mode, and nothing
  authenticates the packets.
- ssh protects the login and the secret, and nothing after it.

Run udr inside a tunnel (WireGuard, `ssh -w`) when the data matters.

## Build and tests

- Only the ssh form (`host:path`) works. The `host::module` form of the original
  has no server to talk to.
- `tests/smoke.sh` copies a tree through udr on loopback with every cipher, using a
  stand-in `ssh` that runs the remote command on the same host. The package build
  runs it.
