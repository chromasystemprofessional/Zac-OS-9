# The package repository

The signed flat Debian repository that TTOS, or any other system, can point
APT or Turbo at. CI builds it from the tested release packages and publishes
it to the `repo` branch; GitHub Pages serves that branch as a static site.

## What is in it

The repository is flat: the `.deb` files and the metadata sit in the same
directory, with no `dists/` or `pool/`.

| File | What it is |
|---|---|
| `*.deb` | the packages, one per release and version |
| `*.source.tar.gz` | source for a GPL package that needs it, such as the emulators |
| `Packages` | the package index |
| `Packages.gz` | the same, compressed |
| `Release` | the suite metadata and the index checksums |
| `InRelease` | `Release`, clearsigned |
| `Release.gpg` | `Release`, detached-signed |
| `zacos9-archive-keyring.gpg` | the public key, binary |
| `zacos9-archive-keyring.asc` | the same key, armored |
| `.nojekyll` | tells GitHub Pages not to run Jekyll |

The source line for this layout is:

```text
deb [signed-by=/usr/share/keyrings/zacos9-archive-keyring.gpg] \
    https://OWNER.github.io/REPO/ ./
```

## Building it locally

```sh
scripts/build-debs.sh
GNUPGHOME=$HOME/.gnupg scripts/build-repo.sh --key FINGERPRINT
```

The default output is `build/repo`; the default package directory is
`build/packages`. `--suite`, `--codename`, `--origin` and `--label` set the
`Release` fields. The current codename is `trixie`; it becomes `forky` with
the Debian 14 port.

The output must be a clean directory, not a git checkout: `apt-ftparchive`
would otherwise checksum `.git`. Old `.deb` files already in the output are
kept, so a new release adds to the repository instead of replacing it. With a
protected key, gpg prompts for the passphrase; CI sets
`ZACOS9_REPO_GPG_PASSPHRASE` so it does not prompt.

Debug-symbol packages (`*-dbgsym_*.deb`) are left out, because the main
package's debug symbols are larger than GitHub's 100 MB file limit. Set
`ZACOS9_REPO_DEBUG=1` to include them in a local repository.

## The signing key

Make a dedicated key once:

```sh
gpg --quick-generate-key "ZacOS 9 Archive Signing Key <you@example.com>" \
    rsa4096 sign 0
gpg --list-secret-keys --with-colons "ZacOS 9 Archive Signing Key" |
    awk -F: '/^fpr:/ { print $10; exit }'
```

Export the public key and a backup of the private key:

```sh
gpg --armor --export FINGERPRINT > zacos9-archive-keyring.asc
gpg --export FINGERPRINT > zacos9-archive-keyring.gpg
gpg --armor --export-secret-keys FINGERPRINT > zacos9-repo-private.asc
```

Keep `zacos9-repo-private.asc` off the repository and back it up. Keep the
revocation certificate too. Publish the fingerprint somewhere other than the
repository, so a user can check the key they downloaded.

For CI, add two repository secrets:

| Secret | Value |
|---|---|
| `ZACOS9_REPO_GPG_PRIVATE_KEY` | `base64 -w0 zacos9-repo-private.asc` |
| `ZACOS9_REPO_GPG_PASSPHRASE` | the key's passphrase, or empty |

The private key is never committed. CI imports it into `$HOME/.gnupg` for the
length of the job.

## Publishing

`.github/workflows/debian.yml` has a `repo` job that runs only on `v*` tags,
after the package job passes:

1. download the tested packages from the run's artifact;
2. import the signing key;
3. clone the current `repo` branch and copy its old packages aside;
4. run `scripts/build-repo.sh`;
5. sign the release's `SHA256SUMS` and upload `SHA256SUMS.asc` to the
   GitHub release;
6. replace the branch's contents with the new repository and push.

One-time setup: enable GitHub Pages for the repository, with **Deploy from a
branch**, branch `repo`, folder `/ (root)`. The site is then
`https://OWNER.github.io/REPO/`. Do not protect the `repo` branch; CI writes
it. The first tag creates it.

## Release assets

The GitHub release carries more than the repository branch: the packages,
the emulators' GPL source, a CycloneDX SBOM (`zacos9-<version>.cdx.json`)
and `SHA256SUMS` with `SHA256SUMS.asc`, a detached signature made with the
archive key above. The SBOM and signature are release assets, not part of
the `repo` branch. `scripts/release.sh` writes and signs them for a local
release; the `repo` job signs the `v*` assets again from the tested
artifacts. [docs/security.md](security.md) has the verification commands.

If the signing secret is missing, the job fails before it publishes anything,
and the release is left with the package job's unsigned assets. Fix the
secret and re-run the tag's workflow to sign `SHA256SUMS` and publish the
repository.

## Using it

APT:

```sh
sudo install -m 0644 zacos9-archive-keyring.gpg /usr/share/keyrings/
echo 'deb [signed-by=/usr/share/keyrings/zacos9-archive-keyring.gpg] https://OWNER.github.io/REPO/ ./' |
    sudo tee /etc/apt/sources.list.d/zacos9.list
sudo apt update
sudo apt install zacos9-session
```

Until the session split lands, the package is named `zacos9`.

Turbo uses the same URL and public key. The repository is a plain static file
tree, so it also works from a server, a SourceForge folder or a GitHub
release page that keeps the same layout. Tom's XML repository configuration
only needs the base URL and the key.

## Testing it

```sh
meson test -C build repo
```

or directly:

```sh
python3 tests/repo/test_repo.py scripts/build-repo.sh
```

The test makes a temporary signing key and two tiny packages, builds the
repository twice, and checks that the second run keeps the first package. It
verifies `InRelease` and `Release.gpg`, then runs `apt-get update` and
`apt-get download` against a `file://` source, so the layout, hashes and key
are exercised together. It skips when `apt-utils`, `gnupg` or `dpkg-deb` is
missing.

## Notes

- GitHub Pages has a 1 GB site limit and a 100 GB/month soft bandwidth limit.
  A ZacOS repository is small, but prune old versions if it grows. The
  metadata is regenerated from whatever `.deb` files are present.
- The `repo` branch itself is not signed; `Release` and `InRelease` are.
- If the key is rotated, publish the new keyring and sign the repository with
  both keys for one release, so installed systems can verify either.
- A public repository does not need SFTP or encrypted logins. Signed metadata
  is what proves the packages came from ZacOS.
