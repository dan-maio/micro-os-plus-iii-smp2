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
| `micro-os-plus-iii-smp2` | `main` | **this repository**: the current version, all parts in one, each with its full history |
| `micro-os-plus-iii` | `smp` | the earlier version of the kernel (also `align/smp-upstream`, behind the current one) |
| `micro-os-plus-iii-aarch32` | `smp` | the earlier AArch32 port |
| `micro-os-plus-iii-aarch64` | `smp` | the earlier AArch64 port |
| `micro-os-plus-iii-cortexm` | `smp` | the earlier Cortex-M port |
| `micro-os-plus-iii-posix-arch` | `smp` | the earlier POSIX port |

The per-part repositories also carry older branches (`master`, `xpack`,
`xpack-development`, `smp-back`, ...). The earlier version is the branch
`smp`, and this repository leaves it as it is.

The five device packages (`rk3506`, `bcm283x`, `storage`, `rp2xxx`,
`stm32f4`) and the alignment records have no repository of their own on
GitHub: they are only in this one.

The kernel's upstream is `micro-os-plus/micro-os-plus-iii` (remote
`upstream` in the kernel's working repository); it is read-only here.

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
git clone https://github.com/dan-maio/micro-os-plus-iii-smp2.git
cd micro-os-plus-iii-smp2
git log --oneline -5                          # the whole project
git log --oneline -- cortexm/micro-os-plus-iii-cortexm   # one part
```

One clone gets everything: the kernel and its tests, the ports, the device
packages and the records, in the layout the build expects (section 3.5 of
`WORKING-WITH-LOCAL-BARE.md`).

Build the tests from it the usual way (`micro-os-plus-iii/tests`, xpm), or
make a self-contained test workspace with `micro-os-scaffold`:

```bash
node bin/scaffold.js /path/to/workspace \
  --source /path/to/micro-os-plus-iii-smp2 \
  --select aarch64-rpi4b.smp_test0 --yes
```

`micro-os-plus-iii/tests/xpacks/` is git-ignored, so a clone has none of
the packages `xpm install` puts there (build-helper, chan-fatfs, the
CMSIS RTOS validator). Run `xpm install` in `micro-os-plus-iii/tests`, or
let `micro-os-scaffold` copy them from the xPack store.

---

## 4. The remotes

In this repository's local clone (`/home/dan/Work/micro-os-plus-iii-smp2`):

```bash
git remote -v
origin  https://github.com/dan-maio/micro-os-plus-iii-smp2.git (fetch/push)
```

In the per-part working repositories of the workspace
(`/home/dan/Desktop/micro-os-plu-iii-smp-pr-github`):

| Repository | `origin` (GitHub) | `local` (bare backup) | other |
|---|---|---|---|
| kernel | `dan-maio/micro-os-plus-iii` | yes | `upstream`, `golden` |
| aarch32, aarch64, cortexm, posix-arch ports | `dan-maio/micro-os-plus-iii-<arch>` | yes | `golden` |
| the five device packages | none | yes | none |
| the workspace records | none | yes (`workspace.git`) | none |

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

Edit, commit and push here:

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
git subtree push --prefix=cortexm/micro-os-plus-iii-cortexm \
  /home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local/micro-os-plus-iii-cortexm.git \
  align/smp-upstream
```

`git subtree push` rewrites the part's commits without the folder prefix
and pushes them as new commits on that branch. Use it only when the part
also has to be published on its own.

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

All parts in one go (a part with nothing new merges nothing):

```bash
L=/home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local
cd /home/dan/Work/micro-os-plus-iii-smp2

for p in micro-os-plus-iii:micro-os-plus-iii \
         aarch32/micro-os-plus-iii-aarch32:micro-os-plus-iii-aarch32 \
         aarch32/micro-os-plus-iii-devices-rk3506:micro-os-plus-iii-devices-rk3506 \
         aarch64/micro-os-plus-iii-aarch64:micro-os-plus-iii-aarch64 \
         common/micro-os-plus-iii-devices-bcm283x:micro-os-plus-iii-devices-bcm283x \
         common/micro-os-plus-iii-devices-storage:micro-os-plus-iii-devices-storage \
         cortexm/micro-os-plus-iii-cortexm:micro-os-plus-iii-cortexm \
         cortexm/micro-os-plus-iii-devices-rp2xxx:micro-os-plus-iii-devices-rp2xxx \
         cortexm/micro-os-plus-iii-devices-stm32f4:micro-os-plus-iii-devices-stm32f4 \
         posix-arch/micro-os-plus-iii-posix-arch:micro-os-plus-iii-posix-arch; do
  dir=${p%%:*}; repo=${p#*:}
  echo "== $dir"
  git subtree pull --prefix="$dir" "$L/$repo.git" align/smp-upstream -m "smp2: update $dir"
done
git push origin main
```

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
git push origin main
```

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

## 9. The earlier version (branch `smp`)

Read it without touching your work:

```bash
cd /home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii
git fetch origin smp
git log --oneline -5 origin/smp
git diff --stat origin/smp align/smp-upstream
git worktree add /tmp/kernel-smp origin/smp     # a separate checkout of it
git worktree remove /tmp/kernel-smp
```

Do not push `align/smp-upstream` onto `smp`. The per-part repositories
keep the earlier version, and this repository holds the current one.

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
| Clone this repository | `git clone https://github.com/dan-maio/micro-os-plus-iii-smp2.git` |
| History of one part | `git log --oneline -- <folder>` |
| Pull | `git pull --ff-only origin main` |
| Push | `git push origin main` |
| Preview a push | `git push --dry-run origin main` |
| Bring one part's new commits in | `git subtree pull --prefix=<folder> <bare>.git align/smp-upstream` |
| Bring the records in | `git pull --no-rebase <workspace.git> align/smp-upstream` |
| Send one part's commits out | `git subtree push --prefix=<folder> <bare>.git align/smp-upstream` |
| Read the earlier version | `git fetch origin smp`, `git log origin/smp` (in a per-part repository) |
| Compare with GitHub, no fetch | `git ls-remote origin refs/heads/main` |

---

## 12. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Authentication failed`, `403` | not signed in, or the token lacks `repo` | `gh auth status`, `gh auth login`, `gh auth setup-git` |
| git asks for a password | the `gh` credential helper is not set | `gh auth setup-git` |
| `! [rejected] … (non-fast-forward)` | GitHub has commits you do not | `git fetch origin`, rebase or merge, push again |
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
