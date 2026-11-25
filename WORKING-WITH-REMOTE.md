# Working with the GitHub remotes

*What is on GitHub for µOS++ III SMP, how to sign in, clone, pull and push,
and how this repository (`micro-os-plus-iii-smp2`) and the per-part
repositories stay in step. The commands are written out.*

The local bare backups are described in `WORKING-WITH-LOCAL-BARE.md`. This
guide covers GitHub. Both work the same way in git: a remote is a name for
a URL, and each `git push` writes to one remote.

---

## 1. What is on GitHub

Everything is under the account `dan-maio`:

| Repository | Branch | What it holds |
|---|---|---|
| `micro-os-plus-iii-smp2` | `main` | **this repository**: the current version, all parts in one; the kernel and the device packages with their full history, the four ports as submodules |
| `micro-os-plus-iii` | `smp` | the earlier version of the kernel (also `align/smp-upstream`, behind the current one) |
| `micro-os-plus-iii-aarch32` | `smp` | the current AArch32 port, submodule `aarch32/micro-os-plus-iii-aarch32` here |
| `micro-os-plus-iii-aarch64` | `smp` | the current AArch64 port, submodule `aarch64/micro-os-plus-iii-aarch64` here |
| `micro-os-plus-iii-cortexm` | `smp` | the current Cortex-M port, submodule `cortexm/micro-os-plus-iii-cortexm` here |
| `micro-os-plus-iii-posix-arch` | `smp` | the current POSIX port, submodule `posix-arch/micro-os-plus-iii-posix-arch` here |

The per-part repositories also carry older branches (`master`, `xpack`,
`xpack-development`, `smp-back`, ...). For the kernel, `smp` is the earlier
version and this repository leaves it as it is. For the four ports, `smp`
was fast-forwarded on 2026-11-25 to the port's `align/smp-upstream` (same
commits, same hashes), so it holds the current version; the earlier one is
an ancestor on it (`b79d35d` AArch32, `cb16868` AArch64, `e0a7ceb`
Cortex-M, `dcd8099` POSIX).

The `smp` branch of each port has a protection rule: no force push, no
deletion, admins included. "Lock branch" is off, so ordinary
(fast-forward) pushes work; with it on, every push is refused with
`protected branch hook declined`.

The five device packages (`rk3506`, `bcm283x`, `storage`, `rp2xxx`,
`stm32f4`) and the alignment records have no repository of their own on
GitHub: they are only in this one.

The kernel's upstream is `micro-os-plus/micro-os-plus-iii`; it is
read-only here (no remote points to it, fetch it by URL when needed).

---

## 2. Signing in

git reaches GitHub over HTTPS, with the GitHub CLI (`gh`) as credential
helper:

```bash
gh auth status          # Logged in to github.com account dan-maio
gh auth setup-git       # once per machine: git uses gh for github.com
git config --global --get-regexp 'credential.*github'
# credential.https://github.com.helper !/usr/bin/gh auth git-credential
```

`gh auth login` signs in (browser or token). The token needs the `repo`
scope to push. Never put a token in a URL or a remote: `gh` hands it to
git when git asks.

SSH also works if you prefer it: add your public key to the GitHub
account, then use `git@github.com:dan-maio/<repository>.git` as the URL
(`git remote set-url origin git@github.com:dan-maio/micro-os-plus-iii-smp2.git`).

---

## 3. Clone this repository

```bash
git clone --recurse-submodules https://github.com/dan-maio/micro-os-plus-iii-smp2.git
cd micro-os-plus-iii-smp2
git log --oneline -5                                        # the whole project
git log --oneline -- cortexm/micro-os-plus-iii-devices-stm32f4   # the kernel or a device package
git -C cortexm/micro-os-plus-iii-cortexm log --oneline -5   # a port (submodule)
git submodule status                                        # the four ports and their commits
```

One clone with `--recurse-submodules` gets everything: the kernel and its
tests, the ports, the device packages and the records, in the layout the
build expects (section 3.5 of `WORKING-WITH-LOCAL-BARE.md`). Without it the
four port folders are empty; fill them with:

```bash
git submodule update --init
```

Each port is checked out at the commit this repository records, on a
detached HEAD. To work in a port, put it on its branch first (section 6.1):

```bash
git -C cortexm/micro-os-plus-iii-cortexm switch smp
```

To make `git pull` also update the submodules to the commits it brings,
once per clone:

```bash
git config submodule.recurse true
```

### 3.1 Build and test a fresh clone

A clone has no `xpacks/` and no `build/` folders (both git-ignored), so
before the first build every configuration needs `xpm install` and
`xpm run link-deps`. Everything runs in `micro-os-plus-iii/tests`:

```bash
cd micro-os-plus-iii/tests
xpm install                        # once: build-helper, chan-fatfs, validator

C=aarch64-rpi3b-cmake-gcc          # <platform>-cmake-gcc
for v in debug release; do
  xpm install --config $C-$v       # this configuration's tools
  xpm run link-deps --config $C-$v # link the ports from this repository
done
xpm run test-aarch64-rpi3b-cmake   # prepare, build, test: Debug and Release
```

`link-deps` runs `scripts/link-local.sh`, which links
`build/<config>/xpacks/@micro-os-plus/<package>` to the package's folder in
this repository with a **relative** path, for example:

```text
build/aarch64-rpi3b-cmake-gcc-debug/xpacks/@micro-os-plus/micro-os-plus-iii-aarch64
  -> ../../../../../../aarch64/micro-os-plus-iii-aarch64
```

The clone can therefore be anywhere and be moved. Only the toolchains
(GCC, QEMU, CMake, OpenOCD) and the external packages (Segger RTT and
SystemView, Pico SDK, chan-fatfs) come from the xPack store in
`~/.local/xPacks`. Do not use `xpm link @micro-os-plus/<package>`: it
links to whatever working copy was last registered on the machine.

Without `link-deps` the configure step stops with:

```text
micro-os-plus-iii-cortexm is not linked into this build folder.
Run: xpm run link-deps --config <configuration>
```

If the submodules were not checked out, `link-deps` itself stops with
`link-local: micro-os-plus-iii-cortexm not found under …/`: run
`git submodule update --init` at the repository root.

The QEMU test actions and their configurations (each action runs the
`-debug` and the `-release` configuration, so link both):

| Action | Configurations |
|---|---|
| `test-qemu-cortex-m0-cmake` (also `m3`, `m4f`, `m7f`) | `qemu-cortex-m0-cmake-gcc-{debug,release}` |
| `test-aarch32-rpi-zero-2w-cmake`, `test-aarch32-rpi3b-cmake` | `aarch32-<board>-cmake-gcc-{debug,release}` |
| `test-aarch64-rpi-zero-2w-cmake`, `test-aarch64-rpi3b-cmake`, `test-aarch64-rpi4b-cmake` | `aarch64-<board>-cmake-gcc-{debug,release}` |

A fresh clone in `/tmp` was checked this way on 2026-11-25: all 18 QEMU
configurations (9 platforms, Debug and Release) configure, build and pass
their tests, with no source compiled from outside the clone.

### 3.2 A self-contained test workspace

Or make a self-contained test workspace with `micro-os-scaffold`, which
copies the packages from the xPack store:

```bash
node bin/scaffold.js /path/to/workspace \
  --source /path/to/micro-os-plus-iii-smp2 \
  --select aarch64-rpi4b.smp_test0 --yes
```

---

## 4. The remotes

In this repository's local clone (`/home/dan/Work/micro-os-plus-iii-smp2`):

```bash
git remote -v
origin  https://github.com/dan-maio/micro-os-plus-iii-smp2.git (fetch/push)
```

Each port submodule is a clone of its own GitHub repository, with `origin`
pointing there:

```bash
git -C aarch64/micro-os-plus-iii-aarch64 remote -v
origin  https://github.com/dan-maio/micro-os-plus-iii-aarch64.git (fetch/push)
```

In the per-part working repositories of the workspace
(`/home/dan/Desktop/micro-os-plu-iii-smp-pr-github`):

| Repository | `local` (bare backup) | `golden` |
|---|---|---|
| kernel, aarch32, aarch64, cortexm, posix-arch ports | `/home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local/<name>.git` | `/home/dan/Work2/micro-os-plus/<name>` |
| the five device packages | `/home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local/<name>.git` | none |
| the workspace records | `/home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local/workspace.git` | none |

None of them has an `origin` remote: the workspace repositories do not
point to GitHub. GitHub is reached from this repository (`origin` above),
or by URL (section 9).

No branch tracks an upstream, so name the remote and the branch on every
`push` and `pull`, as the commands below do.

---

## 5. Pull

```bash
git fetch origin
git log --oneline HEAD..origin/main      # what GitHub has that you do not
git pull --ff-only origin main
```

`--ff-only` stops instead of creating a merge commit when the two sides
have diverged. Then look (`git log --oneline --graph --all -20`) and
either rebase (`git rebase origin/main`) or merge (`git merge origin/main`).

---

## 6. Two ways to work

### 6.1 Work in this repository

The kernel, the device packages and the records: edit, commit and push
here:

```bash
git switch main
# ... edit, build, test ...
git add -A && git commit -m "..."
git push origin main
```

That is all GitHub needs. The per-part repositories and their bare
backups are then behind. To carry one part's new commits back to its own
repository, split them out with `git subtree push`:

```bash
git subtree push --prefix=cortexm/micro-os-plus-iii-devices-stm32f4 \
  /home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local/micro-os-plus-iii-devices-stm32f4.git \
  align/smp-upstream
```

`git subtree push` rewrites the part's commits without the folder prefix
and pushes them as new commits on that branch. Use it only when the part
also has to be published on its own.

A port is a repository of its own inside this one. Commit in the port and
push it to its GitHub repository **first**, then record the new commit
here:

```bash
P=aarch64/micro-os-plus-iii-aarch64
git -C $P switch smp                      # leave the detached HEAD (section 3)
# ... edit, build, test ...
git -C $P add -A && git -C $P commit -m "..."
git -C $P push origin smp                 # the port, on GitHub

git add $P                                # this repository records the new commit
git commit -m "smp2: update $P"
git push origin main
```

In that order a clone of this repository never points at a port commit
GitHub does not have. `git push --recurse-submodules=check origin main`
refuses the push if it would (section 7).

### 6.2 Work in the per-part repositories, then update this one

This is how the work was done until now: commit in the workspace's
repositories, back them up to `local`, then bring the new commits into
this repository part by part:

```bash
S=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github
L=/home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local
cd /home/dan/Work/micro-os-plus-iii-smp2

git subtree pull --prefix=micro-os-plus-iii "$L/micro-os-plus-iii.git" align/smp-upstream \
  -m "smp2: update micro-os-plus-iii"
git push origin main
```

Pull from the **bare backups**, not from the working repositories: the
kernel, Cortex-M and POSIX working repositories are shallow clones, and git
refuses to fetch from a shallow repository into a full one.

All subtree parts in one go (a part with nothing new merges nothing):

```bash
L=/home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local
cd /home/dan/Work/micro-os-plus-iii-smp2

for p in micro-os-plus-iii:micro-os-plus-iii \
         aarch32/micro-os-plus-iii-devices-rk3506:micro-os-plus-iii-devices-rk3506 \
         common/micro-os-plus-iii-devices-bcm283x:micro-os-plus-iii-devices-bcm283x \
         common/micro-os-plus-iii-devices-storage:micro-os-plus-iii-devices-storage \
         cortexm/micro-os-plus-iii-devices-rp2xxx:micro-os-plus-iii-devices-rp2xxx \
         cortexm/micro-os-plus-iii-devices-stm32f4:micro-os-plus-iii-devices-stm32f4; do
  dir=${p%%:*}; repo=${p#*:}
  echo "== $dir"
  git subtree pull --prefix="$dir" "$L/$repo.git" align/smp-upstream -m "smp2: update $dir"
done
git push origin main
```

The ports are not subtrees any more. Push the port's `align/smp-upstream`
from its working repository to `smp` on GitHub (a fast-forward, the two
hold the same history), then move the submodule here to it:

```bash
S=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github
cd /home/dan/Work/micro-os-plus-iii-smp2

for p in aarch32/micro-os-plus-iii-aarch32 aarch64/micro-os-plus-iii-aarch64 \
         cortexm/micro-os-plus-iii-cortexm posix-arch/micro-os-plus-iii-posix-arch; do
  echo "== $p"
  git -C "$S/$p" push "https://github.com/dan-maio/${p#*/}.git" align/smp-upstream:smp
done
git submodule update --remote          # each port to the tip of its smp
git add aarch32/micro-os-plus-iii-aarch32 aarch64/micro-os-plus-iii-aarch64 \
        cortexm/micro-os-plus-iii-cortexm posix-arch/micro-os-plus-iii-posix-arch
git commit -m "smp2: update the ports"
git push origin main
```

A port whose `smp` already has every commit reports `Everything up-to-date`.
If GitHub refuses with `non-fast-forward`, the port was also changed here
(section 6.1): merge the two in the port before pushing.

The alignment records (the files at this repository's root and
`workspace-records/`) are this repository's own base history. Bring new
record commits in with a plain merge:

```bash
git pull --no-rebase /home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local/workspace.git align/smp-upstream
```

Choose one way and keep to it. Editing the same part both here and in its
own repository gives two diverging histories that have to be merged by
hand.

---

## 7. Push

```bash
git push --dry-run origin main     # what would be sent; writes nothing
git push --recurse-submodules=check origin main
```

`--recurse-submodules=check` refuses the push if this repository records a
port commit that is not on the port's GitHub repository yet (push the port
first, section 6.1). `git config push.recurseSubmodules check` makes it the
default.

A rejected push (`non-fast-forward`, `fetch first`) means GitHub has
commits you do not: `git fetch origin`, integrate (section 5), push again.
Do not force-push `main`. If history really has to be replaced, use
`git push --force-with-lease origin main`, which refuses if GitHub moved
since your last fetch.

To keep a local bare backup of this repository as well:

```bash
git init --bare /home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local/micro-os-plus-iii-smp2.git
git remote add local /home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local/micro-os-plus-iii-smp2.git
git push origin main && git push local main     # GitHub first, the backup last
```

---

## 8. Before pushing anything public

This repository is public. Nothing pushed can be taken back reliably.

- Wi-Fi test credentials live only in the CMake cache of a build folder
  (`-D WIFI_TEST_SSID=... -D WIFI_TEST_PASS=...`). Build folders, xPack
  folders and the records' large snapshots are git-ignored. Never add them
  with `git add -f`.
- Check what a push carries:

  ```bash
  git log --oneline origin/main..main
  git diff --stat origin/main main
  ```

- Search the new commits for a value you must not publish, without
  printing it (replace the variable with the value, typed at the prompt):

  ```bash
  read -rs SECRET
  git log -p origin/main..main | grep -c -F -- "$SECRET"   # must print 0
  unset SECRET
  ```

The history pushed on 2026-11-24 was searched this way for the Wi-Fi
values: no commit and no file contains them.

---

## 9. The earlier version

For the kernel, the earlier version is the branch `smp` of
`dan-maio/micro-os-plus-iii`. Read it without touching your work:

```bash
cd /home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii
git fetch https://github.com/dan-maio/micro-os-plus-iii.git smp:refs/remotes/github/smp
git log --oneline -5 github/smp
git diff --stat github/smp align/smp-upstream
git worktree add /tmp/kernel-smp github/smp     # a separate checkout of it
git worktree remove /tmp/kernel-smp
```

The workspace repositories have no `origin` (section 4), so the URL is
given in full. To keep a remote for it instead:
`git remote add github https://github.com/dan-maio/micro-os-plus-iii.git`,
then `git fetch github smp`.

Do not push the kernel's `align/smp-upstream` onto its `smp`: that
repository keeps the earlier version, and this one holds the current one.

For the four ports, `smp` is the current version (section 1). Their earlier
version is an older commit on that same branch; check it out in the
submodule, then go back:

```bash
P=aarch64/micro-os-plus-iii-aarch64
git -C $P switch --detach cb16868    # AArch32 b79d35d, Cortex-M e0a7ceb, POSIX dcd8099
git submodule update $P              # back to the commit this repository records
```

---

## 10. Pull requests

```bash
gh repo view dan-maio/micro-os-plus-iii-smp2 --web
git switch -c my-change
# ... commit ...
git push origin my-change
gh pr create --repo dan-maio/micro-os-plus-iii-smp2 --base main --head my-change
```

---

## 11. Cheat sheet

| Task | Command |
|---|---|
| Sign in / check | `gh auth login`, `gh auth status` |
| Let git use `gh` for GitHub | `gh auth setup-git` |
| Clone this repository | `git clone --recurse-submodules https://github.com/dan-maio/micro-os-plus-iii-smp2.git` |
| Fill the port folders of a plain clone | `git submodule update --init` |
| List the ports and their commits | `git submodule status` |
| Prepare a configuration | in `micro-os-plus-iii/tests`: `xpm install`, `xpm install --config <c>`, `xpm run link-deps --config <c>` |
| Build and test on QEMU | `xpm run test-<platform>-cmake` (Debug and Release) |
| Read the kernel's earlier version | `git fetch https://github.com/dan-maio/micro-os-plus-iii.git smp:refs/remotes/github/smp` |
| History of the kernel or a device package | `git log --oneline -- <folder>` |
| History of a port | `git -C <port folder> log --oneline` |
| Pull | `git pull --ff-only origin main`, then `git submodule update` |
| Push | `git push --recurse-submodules=check origin main` |
| Preview a push | `git push --dry-run origin main` |
| Commit in a port | `git -C <port> switch smp`, commit, `git -C <port> push origin smp`, then `git add <port>` and commit here (section 6.1) |
| Move the ports to the tips of their `smp` | `git submodule update --remote`, then `git add` the four ports and commit |
| Bring a subtree part's new commits in | `git subtree pull --prefix=<folder> <bare>.git align/smp-upstream` |
| Bring the records in | `git pull --no-rebase <workspace.git> align/smp-upstream` |
| Send a subtree part's commits out | `git subtree push --prefix=<folder> <bare>.git align/smp-upstream` |
| Compare with GitHub, no fetch | `git ls-remote origin refs/heads/main` |

---

## 12. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Authentication failed`, `403` | not signed in, or the token lacks `repo` | `gh auth status`, `gh auth login`, `gh auth setup-git` |
| git asks for a password | the `gh` credential helper is not set | `gh auth setup-git` |
| `! [rejected] … (non-fast-forward)` | GitHub has commits you do not | `git fetch origin`, rebase or merge, push again |
| `… is not linked into this build folder` at configure | `link-deps` not run for that configuration | `xpm install --config <c>`, `xpm run link-deps --config <c>` (section 3.1) |
| a port folder is empty; `link-local: … not found` | cloned without `--recurse-submodules` | `git submodule update --init` (section 3) |
| `fatal: … not our ref` or `upload-pack: not our ref` during `git submodule update` | this repository records a port commit that was never pushed to GitHub | push that commit from the port where it was made, then update again |
| `The following submodule paths contain changes that can not be found on any remote` | `git push --recurse-submodules=check` found an unpushed port commit | `git -C <port> push origin smp`, then push here (section 6.1) |
| `! [remote rejected] smp -> smp (protected branch hook declined)` in a port | "Lock branch" is on for that port's `smp` | turn it off: the repository's Settings → Branches → the `smp` rule, untick "Lock branch", Save changes |
| a commit in a port is "lost" after `git submodule update` | the port was on a detached HEAD when you committed | `git -C <port> reflog` finds it; `git -C <port> switch smp` and `git merge <commit>` |
| `'origin' does not appear to be a git repository` in a workspace repository | the workspace repositories have no `origin` | fetch by URL (section 9) |
| `shallow roots are not allowed to be updated` | `git subtree pull` from a shallow working repository | pull from the part's bare backup instead (section 6.2) |
| `Working tree has modifications` from `git subtree` | uncommitted changes | commit or stash them first |
| conflicts after `git subtree pull` | the part was edited both here and in its own repository | resolve, commit, and keep to one way of working (section 6) |
| a pushed file should not have been public | it was added by mistake | delete it and push; the history still holds it, so treat the content as exposed (change any credential) |

---

## 13. State on 2026-11-24

| | Commit |
|---|---|
| this repository, `origin/main` | the README commit, `406c75f2`, and later |
| kernel in this repository | `06d365fe` (same as its bare backup) |
| kernel on GitHub, `align/smp-upstream` | `b9887856`, behind |
| the four ports and five device packages in this repository | the same commits as their bare backups |
| the per-part GitHub repositories, branch `smp` | unchanged (the earlier version) |

## 14. State on 2026-11-25

| | Commit |
|---|---|
| the ports' GitHub repositories, branch `smp` | fast-forwarded to `align/smp-upstream`: AArch32 `2eb3719`, AArch64 `7141456`, Cortex-M `a59271c`, POSIX `673a30e` (the same commits as their bare backups) |
| the four ports in this repository | submodules at those commits; the subtree folders removed |
| kernel and five device packages in this repository | unchanged, still subtrees |
| kernel on GitHub, branch `smp` | unchanged (the earlier version) |
