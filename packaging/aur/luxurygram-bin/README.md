# luxurygram-bin (AUR)

Arch Linux package that repackages the official `LuxuryGram-<version>-linux-x64.tar.xz` release archive. It does not build from source.

What it installs:

- `/usr/bin/LuxuryGram` and a `luxurygram` symlink;
- the desktop entry, AppStream metainfo, D-Bus service and `tg://` / `tonsite://` handlers from `lib/xdg`;
- hicolor icons 16–512 px and the symbolic tray icons;
- `/usr/share/LuxuryGramDesktop/externalupdater.d/luxurygram-bin.conf`, which turns off the built-in updater: pacman owns updates, and the app's update button opens the Releases page instead.

All sources are pinned to the `luxury-v<version>` tag with SHA-256 sums.

## First publication

1. Create an account at <https://aur.archlinux.org/register> and add an SSH public key under *My Account → SSH Public Key*.
2. Point SSH at that key (`~/.ssh/config`):

   ```
   Host aur.archlinux.org
     User aur
     IdentityFile ~/.ssh/aur
   ```

3. Push the package. The clone of a new package name is empty, which is expected:

   ```
   git clone ssh://aur@aur.archlinux.org/luxurygram-bin.git
   cp PKGBUILD .SRCINFO luxurygram-bin/
   cd luxurygram-bin
   git add PKGBUILD .SRCINFO
   git commit -m "luxurygram-bin 1.0.19-1"
   git push origin master
   ```

Users then install it with `yay -S luxurygram-bin` or `paru -S luxurygram-bin`.

## New release

1. Set `pkgver` to the new version and `pkgrel=1`.
2. Replace the first `sha256sums` entry with the archive line from the release's `SHA256SUMS.txt`. Recompute the others (`updpkgsums` on Arch) only if `lib/xdg`, the icons or `LICENSE` changed at the new tag.
3. Regenerate `.SRCINFO` with `makepkg --printsrcinfo > .SRCINFO`, build once with `makepkg -f`, and push both files.
