# FM Buddy 2.0 — the web flasher

A browser install page, like konsumer's at `konsumer.js.org/fm1buddy`, but with one difference that
matters: **the default path keeps your settings.**

---

## Putting it online

The whole thing is static files. GitHub Pages serves them over HTTPS, which Web Serial requires.

1. Copy the contents of this folder into **`docs/`** at the root of your repo:

   ```
   docs/index.html
   docs/firmware/fm1buddy-2-app.bin
   docs/firmware/fm1buddy-2-factory.bin
   docs/firmware/boot_app0.bin
   docs/firmware/manifest-update.json
   docs/firmware/manifest-full.json
   ```

2. Commit and push.

3. On GitHub: **Settings ▸ Pages ▸ Source: Deploy from a branch**, branch `main`, folder **`/docs`**.

4. A minute later it is live at
   `https://theprinterlightstudio-lab.github.io/fm1buddy_extended/`

`.gitignore` ignores `*.bin`, so force these in once:

```sh
git add -f docs/firmware/*.bin
```

Binaries in git bloat every clone forever. If that bothers you, host the `.bin` files on a GitHub
Release instead and point the manifests at their download URLs — the manifest `path` can be an
absolute URL.

## The two buttons

| | What it writes | Your settings |
|---|---|---|
| **Update** | app at `0x10000`, `boot_app0.bin` at `0xe000` | **kept** — nvs is never written |
| **Full install** | the whole flash from `0x0` | **erased** |

Update is the same two operations the desktop scripts do, which is why settings survive. Full
install is for a board that will not boot.

## What it cannot do

**There is no backup.** A browser cannot read the board's flash back, so this page cannot take a
copy first the way `pcbfun_backup_mac.sh` does. The update path never touches the settings
partition, so settings are safe by construction — but there is no way back to the *exact* firmware
currently on the board unless you already have a copy.

Publish each build as a GitHub Release and that problem goes away: any released version can be put
back from the same page.

## Browser support

Chrome or Edge, **on a computer**. Web Serial does not exist in Safari or Firefox, and not on phones
or tablets. The page detects this and says so rather than showing a dead button.

## Keeping it current

After a build, refresh the two binaries:

```sh
cp .pio/build/pcbfun-s3-28/firmware.bin         docs/firmware/fm1buddy-2-app.bin
cp .pio/build/pcbfun-s3-28/firmware.factory.bin docs/firmware/fm1buddy-2-factory.bin
```

Bump `"version"` in both manifests so people can tell what they are installing. It is `dev` now, and
should become `Arbi002` at release.

## What has and has not been tested

**Verified here:** both manifests are valid JSON; the offsets match the partition table
(`app0` at `0x10000`, `otadata` at `0xe000` in `default_16MB.csv`); `boot_app0.bin` is the stock
8,192-byte Arduino image and begins `01 00 00 00`, which selects app0; the page serves and every
file it references resolves over HTTP.

**Not verified:** the actual flash. There is no board and no Web Serial here, so the install has
never been run from a browser. The first time you click it, have the desktop flasher and a backup
to hand.

One known risk worth naming: `esp-web-tools` is loaded from unpkg at version 10. If that CDN is
down, the page loads but the buttons never appear. Vendoring `install-button.js` into
`docs/firmware/` and pointing the `<script src>` at it removes the dependency.
