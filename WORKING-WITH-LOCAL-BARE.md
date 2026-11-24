# Working with the local bare repositories

*How the `~/Downloads/GIT/micro-os-plu-iii-smp-pr-local/` backups are
laid out, and how to check out, push to and pull from them — with the
commands written out.*

This workspace is at `/home/dan/Desktop/micro-os-plu-iii-smp-pr-github`.
The backup folder is at
`/home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local/`. The two are
**not** a superproject with submodules: every project is an independent
git repository, with its own remote pointing at its own bare repo.

Since 2026-11-24 the same work is also on GitHub as one repository,
`dan-maio/micro-os-plus-iii-smp2` (this repository): every project merged
in its folder with its full history. GitHub, and how this repository and
the per-project repositories stay in step, are described in
`WORKING-WITH-REMOTE.md`. This guide covers the local bare backups.

---

## 1. What the backup folder is

It is one folder of **bare repositories**, one per project:

```
~/Downloads/GIT/micro-os-plu-iii-smp-pr-local/
├── micro-os-plus-iii.git                 kernel
├── micro-os-plus-iii-aarch32.git
├── micro-os-plus-iii-aarch64.git
├── micro-os-plus-iii-cortexm.git
├── micro-os-plus-iii-posix-arch.git
├── micro-os-plus-iii-devices-rk3506.git
├── micro-os-plus-iii-devices-rp2xxx.git
├── micro-os-plus-iii-devices-stm32f4.git
├── micro-os-plus-iii-devices-bcm283x.git
├── micro-os-plus-iii-devices-storage.git
└── workspace.git                         this workspace's records
```

Each one is a real bare repository:

```bash
git --git-dir=~/Downloads/GIT/micro-os-plu-iii-smp-pr-local/micro-os-plus-iii.git \
    config --get core.bare      # true
```

A bare repository has **no working tree**. There is nothing to edit in
it, nothing to `cd` into, and no branch checked out. It is a database of
objects plus branch pointers — a backup and a clone source. Checkout
happens in a *separate* working folder (section 3).

None of the bare repos has an `origin` of its own:

```bash
git --git-dir=.../micro-os-plus-iii.git config --get remote.origin.url
# (empty)
```

So pushing to `local` does **not** forward anything to GitHub. GitHub and
the local backup are two independent destinations; you push to each
explicitly, or to both in one command.

Every bare repo currently carries one branch, `align/smp-upstream`; the
kernel also carries the `v7.2.0-pre.1` tag. There is no `main` there.

---

## 2. The remotes in each working repository

The kernel's checkout has four remotes:

```bash
git -C micro-os-plus-iii remote -v

origin   https://github.com/dan-maio/micro-os-plus-iii.git          (fetch/push)
local    /home/dan/Downloads/GIT/micro-os-plu-iii-smp-pr-local/micro-os-plus-iii.git
golden   /home/dan/Work2/micro-os-plus/micro-os-plus-iii            (read-only reference)
upstream https://github.com/micro-os-plus/micro-os-plus-iii.git     (read-only)
```

| Remote | What it is | Which repositories have it |
|---|---|---|
| `origin` | the per-project GitHub repository (`dan-maio`), holding the earlier version on branch `smp` | the kernel and the four ports |
| `local` | the bare backup in `~/Downloads/GIT/...`: local backup, clone source, offline restore | all eleven |
| `golden` | a read-only reference copy under `~/Work2`: compare against the original tree; never push to it | the kernel and the four ports |
| `upstream` | the kernel's upstream, read-only | the kernel |

The five device packages and the workspace records have `local` only. No
branch tracks an upstream: name the remote and the branch in every `push`
and `pull`, as the commands below do.

The mapping is by repository name; the table below is the complete set.

| Working folder | Bare repository |
|---|---|
| `micro-os-plus-iii/` | `micro-os-plus-iii.git` |
| `aarch32/micro-os-plus-iii-aarch32/` | `micro-os-plus-iii-aarch32.git` |
| `aarch64/micro-os-plus-iii-aarch64/` | `micro-os-plus-iii-aarch64.git` |
| `cortexm/micro-os-plus-iii-cortexm/` | `micro-os-plus-iii-cortexm.git` |
| `cortexm/micro-os-plus-iii-devices-rp2xxx/` | `micro-os-plus-iii-devices-rp2xxx.git` |
| `cortexm/micro-os-plus-iii-devices-stm32f4/` | `micro-os-plus-iii-devices-stm32f4.git` |
| `common/micro-os-plus-iii-devices-bcm283x/` | `micro-os-plus-iii-devices-bcm283x.git` |
| `common/micro-os-plus-iii-devices-storage/` | `micro-os-plus-iii-devices-storage.git` |
| `aarch32/micro-os-plus-iii-devices-rk3506/` | `micro-os-plus-iii-devices-rk3506.git` |
| `posix-arch/micro-os-plus-iii-posix-arch/` | `micro-os-plus-iii-posix-arch.git` |
| `workspace-records/` | `workspace.git` |

---

## 3. Checkout

### 3.1 The bare repo is a source, not a place to work

To turn a backup into a working folder, **clone** it:

```bash
cd /tmp/opencode
git clone ~/Downloads/GIT/micro-os-plu-iii-smp-pr-local/micro-os-plus-iii.git kernel-restore
cd kernel-restore
git branch --show-current        # align/smp-upstream
```

`git clone` of a bare repo checks out its default branch automatically.
If it had several branches and you want another:

```bash
git clone --branch align/smp-upstream \
  ~/Downloads/GIT/micro-os-plu-iii-smp-pr-local/micro-os-plus-iii.git kernel-restore
```

### 3.2 Check out a branch in an existing clone

```bash
git fetch local
git switch --track local/align/smp-upstream      # create a local branch tracking local
# or, if the branch already exists locally:
git switch align/smp-upstream
```

`git switch` is the modern spelling of `git checkout` for branches. The
older names still work:

```bash
git checkout -b align/smp-upstream --track local/align/smp-upstream
git checkout align/smp-upstream
```

### 3.3 Check out one commit without moving the branch

To look at a snapshot — the state the backup holds today, or any earlier
commit — check out the commit itself; you land in **detached HEAD**, which
is fine for reading and building, not for committing:

```bash
git checkout bc6be627          # a snapshot from the backup
git switch -                   # back to the branch you came from
```

If you want to keep work made on a detached HEAD, create a branch before
leaving it:

```bash
git switch -c fix-from-snapshot
```

### 3.4 Several checkouts of one repository

A bare repo can serve any number of worktrees:

```bash
BARE=~/Downloads/GIT/micro-os-plu-iii-smp-pr-local/micro-os-plus-iii.git
git -C "$BARE" worktree add /tmp/opencode/kernel-branch align/smp-upstream
git -C "$BARE" worktree list
git -C "$BARE" worktree remove /tmp/opencode/kernel-branch   # never rm -rf
```

This is optional here: in this workspace each project has one normal
clone, and the bare repos have no worktrees attached.

### 3.5 What a checkout must look like for the build

The test system resolves the ports as **siblings** of the kernel, and the
xpm links point at the actual folders. A restore is only usable for
building if the layout is preserved:

```
micro-os-plus-iii-smp-pr-github/
├── micro-os-plus-iii/                  kernel
├── aarch32/micro-os-plus-iii-aarch32/
├── aarch32/micro-os-plus-iii-devices-rk3506/
├── aarch64/micro-os-plus-iii-aarch64/
├── cortexm/micro-os-plus-iii-cortexm/
├── cortexm/micro-os-plus-iii-devices-rp2xxx/
├── cortexm/micro-os-plus-iii-devices-stm32f4/
├── common/micro-os-plus-iii-devices-bcm283x/
├── common/micro-os-plus-iii-devices-storage/
├── posix-arch/micro-os-plus-iii-posix-arch/
└── workspace-records/
```

After cloning, re-link the packages before building:

```bash
cd micro-os-plus-iii/tests
xpm run link-deps-all
```

---

## 4. Push

### 4.1 The canonical command

From the working repository, on `align/smp-upstream`:

```bash
git push local HEAD:refs/heads/align/smp-upstream
```

`HEAD:refs/heads/…` means "take whatever commit I have checked out and
write it to that branch in the bare repo". It works even on detached HEAD
and even if you never set an upstream. This is the safest form to use in
scripts.

The short form, once the local branch exists on both sides:

```bash
git push local align/smp-upstream
```

### 4.2 Set the upstream once

```bash
git push -u local align/smp-upstream
```

After that, plain `git push local` and `git pull local` know the branch.
The main checkouts here track nothing (section 2), so the commands in this
guide always name the remote and the branch.

### 4.3 Push to GitHub and to the backup

The current version goes to GitHub through this repository,
`micro-os-plus-iii-smp2` (`WORKING-WITH-REMOTE.md`, section 6.2): back the
projects up to `local`, then bring them into `smp2` and push it. The
per-project GitHub repositories keep the earlier version on `smp`; only the
kernel has an `align/smp-upstream` there, and it is behind.

To push a project to its own GitHub repository as well (kernel and ports
only), remember they are separate destinations; one `git push` reaches one
remote:

```bash
git push origin align/smp-upstream        # the public fork
git push local  align/smp-upstream        # the local backup
```

In one line, stopping if GitHub rejects the push:

```bash
git push origin align/smp-upstream && git push local align/smp-upstream
```

Push to `local` **last**: it is the backup, and it should contain exactly
what was published. If the GitHub push fails, the backup still holds the
previous state, which is what you want.

### 4.4 Push every repository

All projects use the same branch name, so a loop is enough. Run it from
the workspace root:

```bash
cd /home/dan/Desktop/micro-os-plu-iii-smp-pr-github

for d in micro-os-plus-iii \
         aarch32/micro-os-plus-iii-aarch32 \
         aarch64/micro-os-plus-iii-aarch64 \
         cortexm/micro-os-plus-iii-cortexm \
         cortexm/micro-os-plus-iii-devices-rp2xxx \
         cortexm/micro-os-plus-iii-devices-stm32f4 \
         common/micro-os-plus-iii-devices-bcm283x \
         common/micro-os-plus-iii-devices-storage \
         aarch32/micro-os-plus-iii-devices-rk3506 \
         posix-arch/micro-os-plus-iii-posix-arch \
         workspace-records; do
  echo "== $d"
  git -C "$d" push local HEAD:refs/heads/align/smp-upstream
done
```

Only the repositories that changed need a push; the others report
`Everything up-to-date` and exit successfully.

### 4.5 Tags

Push a release tag to the backup explicitly:

```bash
git push local v7.2.0-pre.1              # one tag
git push local --tags                    # every tag not already there
```

The kernel backup currently holds `v7.2.0-pre.1`; the other repos hold no
tags.

### 4.6 Check before writing

```bash
git push --dry-run local HEAD:refs/heads/align/smp-upstream
```

This contacts the bare repo, prints what would be transferred, and writes
nothing.

### 4.7 When the push is rejected

`local` is an ordinary repository, so it refuses a non-fast-forward push:
the branch in the bare repo has commits the current checkout does not.
That normally means another clone pushed there, or the checkout is stale.

```bash
git fetch local
git log --oneline --graph --decorate --all -15
```

Then either merge/rebase onto `local/align/smp-upstream` and push again,
or, if the local branch is genuinely the one to keep:

```bash
git push --force-with-lease local HEAD:refs/heads/align/smp-upstream
```

`--force-with-lease` refuses if the remote moved since your last fetch.
Prefer it to `--force`, which would silently discard the other side.

Never force-push `origin` as part of a routine backup. Fix the history,
then push normally.

---

## 5. Pull and fetch

### 5.1 Fetch first, pull second

`git pull` is `git fetch` plus a merge or rebase. To see what arrived
before changing the working tree:

```bash
git fetch local
git log --oneline HEAD..local/align/smp-upstream    # what local has that you do not
git log --oneline local/align/smp-upstream..HEAD    # what you have that local does not
git diff --stat HEAD local/align/smp-upstream
```

Then integrate:

```bash
git pull --ff-only local align/smp-upstream
```

`--ff-only` is the right default for a backup: if a fast-forward is not
possible, stop and look, rather than create a merge commit by accident.

### 5.2 Pull from GitHub

```bash
git fetch origin
git pull --ff-only origin align/smp-upstream
```

Only the kernel has `align/smp-upstream` on its GitHub repository; the
current version of everything is in `micro-os-plus-iii-smp2`
(`WORKING-WITH-REMOTE.md`).

### 5.3 If the two sides have diverged

```bash
git fetch --all
git log --oneline --graph --decorate --all -20
```

Then choose:

```bash
# keep your commits on top of the remote
git rebase origin/align/smp-upstream

# or record a merge
git merge origin/align/smp-upstream
```

After either, re-run the build/tests and push to both remotes again.

### 5.4 When not to pull

`local` is a **backup**, usually behind the working checkout. Pulling
from it is only meaningful when:

- you are restoring on another machine, or
- you pushed from another clone and want those commits here.

On the usual machine, `git fetch local` is enough; it shows whether the
backup is stale without touching the working tree.

---

## 6. Inspect the backups without checking anything out

Everything can be read straight from the bare repo:

```bash
BARE=~/Downloads/GIT/micro-os-plu-iii-smp-pr-local/micro-os-plus-iii.git

git --git-dir="$BARE" for-each-ref \
    --format='%(refname:short) %(objectname:short) %(committerdate:iso)' refs/heads

git --git-dir="$BARE" log --oneline -5 align/smp-upstream
git --git-dir="$BARE" show --stat align/smp-upstream
git --git-dir="$BARE" ls-tree -r --name-only align/smp-upstream | head
git --git-dir="$BARE" diff align/smp-upstream~1 align/smp-upstream
```

### 6.1 Is a backup in sync?

```bash
cd /home/dan/Desktop/micro-os-plu-iii-smp-pr-github

for d in micro-os-plus-iii \
         aarch32/micro-os-plus-iii-aarch32 \
         aarch64/micro-os-plus-iii-aarch64 \
         cortexm/micro-os-plus-iii-cortexm \
         cortexm/micro-os-plus-iii-devices-rp2xxx \
         cortexm/micro-os-plus-iii-devices-stm32f4 \
         common/micro-os-plus-iii-devices-bcm283x \
         common/micro-os-plus-iii-devices-storage \
         aarch32/micro-os-plus-iii-devices-rk3506 \
         posix-arch/micro-os-plus-iii-posix-arch \
         workspace-records; do
  h=$(git -C "$d" rev-parse HEAD)
  l=$(git -C "$d" ls-remote local refs/heads/align/smp-upstream | cut -f1)
  [ "$h" = "$l" ] && echo "$d: in sync" || echo "$d: DIFFERS ($h vs $l)"
done
```

`git ls-remote` talks to the bare repo directly; it needs no fetch and
creates no remote-tracking branch.

---

## 7. Restore a workspace from the backups

The simplest restore is one clone of `micro-os-plus-iii-smp2` from GitHub:
it has every project in this layout (`WORKING-WITH-REMOTE.md`, section 3).
From the local backups, project by project:

```bash
mkdir -p ~/Desktop/restore && cd ~/Desktop/restore
B=~/Downloads/GIT/micro-os-plu-iii-smp-pr-local

git clone "$B/micro-os-plus-iii.git"                 micro-os-plus-iii
git clone "$B/micro-os-plus-iii-aarch32.git"         aarch32/micro-os-plus-iii-aarch32
git clone "$B/micro-os-plus-iii-devices-rk3506.git"  aarch32/micro-os-plus-iii-devices-rk3506
git clone "$B/micro-os-plus-iii-aarch64.git"         aarch64/micro-os-plus-iii-aarch64
git clone "$B/micro-os-plus-iii-cortexm.git"         cortexm/micro-os-plus-iii-cortexm
git clone "$B/micro-os-plus-iii-devices-rp2xxx.git"  cortexm/micro-os-plus-iii-devices-rp2xxx
git clone "$B/micro-os-plus-iii-devices-stm32f4.git" cortexm/micro-os-plus-iii-devices-stm32f4
git clone "$B/micro-os-plus-iii-devices-bcm283x.git" common/micro-os-plus-iii-devices-bcm283x
git clone "$B/micro-os-plus-iii-devices-storage.git" common/micro-os-plus-iii-devices-storage
git clone "$B/micro-os-plus-iii-posix-arch.git"      posix-arch/micro-os-plus-iii-posix-arch
git clone "$B/workspace.git"                         workspace-records
```

Then, in each clone, confirm the branch and restore the normal remote
names (`origin`, `golden` are not copied by `git clone` of a bare repo
that has no remotes):

```bash
cd micro-os-plus-iii
git branch --show-current                    # align/smp-upstream
git remote add origin https://github.com/dan-maio/micro-os-plus-iii.git
git remote add golden /home/dan/Work2/micro-os-plus/micro-os-plus-iii
git remote -v
```

Finally, rebuild the xpm links (section 3.5).

---

## 8. Add another project to the backup set

```bash
B=~/Downloads/GIT/micro-os-plu-iii-smp-pr-local

git init --bare "$B/new-project.git"
git -C path/to/new-project remote add local "$B/new-project.git"
git -C path/to/new-project push -u local HEAD:refs/heads/align/smp-upstream
```

To back up **all** branches of an existing repository instead, use a
mirror clone once:

```bash
git clone --mirror path/to/new-project "$B/new-project.git"
# later updates:
git -C path/to/new-project push --mirror "$B/new-project.git"
```

Do not mix the two styles: a mirror push deletes branches in the bare
repo that the source no longer has. The existing backups use the
single-branch style above.

---

## 9. Cheat sheet

| Task | Command |
|---|---|
| Check out a backup into a working folder | `git clone <bare>.git <folder>` |
| Check out a branch | `git switch --track local/align/smp-upstream` |
| Look at a snapshot | `git checkout <commit>` then `git switch -` |
| Push to the backup | `git push local HEAD:refs/heads/align/smp-upstream` |
| Push to GitHub and the backup | `git push origin align/smp-upstream && git push local align/smp-upstream` |
| Set the upstream once | `git push -u local align/smp-upstream` |
| Preview a push | `git push --dry-run local align/smp-upstream` |
| Fetch the backup | `git fetch local` |
| Pull, fast-forward only | `git pull --ff-only local align/smp-upstream` |
| Push tags | `git push local --tags` |
| Read the backup with no checkout | `git --git-dir=<bare>.git log --oneline -5 align/smp-upstream` |
| Compare, no fetch | `git ls-remote local refs/heads/align/smp-upstream` |
| Replace a diverged backup branch | `git push --force-with-lease local HEAD:refs/heads/align/smp-upstream` |

---

## 10. State on 2026-11-24

Checked with `git ls-remote local` against every working repository:

| Repository | HEAD | `local` | State |
|---|---|---|---|
| `micro-os-plus-iii` | `06d365fe` | `06d365fe` | in sync |
| aarch32 port | `2eb3719d` | `2eb3719d` | in sync |
| aarch64 port | `7141456e` | `7141456e` | in sync |
| cortexm port | `a59271c3` | `a59271c3` | in sync |
| devices-rp2xxx | `9e51139a` | `9e51139a` | in sync |
| devices-stm32f4 | `ea867dfc` | `ea867dfc` | in sync |
| devices-bcm283x | `027acb31` | `027acb31` | in sync |
| devices-storage | `7c6cd981` | `7c6cd981` | in sync |
| devices-rk3506 | `d84cd463` | `d84cd463` | in sync |
| posix-arch port | `673a30e3` | `673a30e3` | in sync |
| workspace records | `3ad11be5` | `3ad11be5` | in sync |

All eleven were in sync on 2026-11-24, and the same commits are in
`micro-os-plus-iii-smp2` on GitHub. On the per-project GitHub
repositories, only the kernel has `align/smp-upstream` (`b9887856`,
behind); their `smp` branches hold the earlier version.

The kernel, Cortex-M and POSIX working repositories are **shallow**
clones (`git rev-parse --is-shallow-repository` prints `true`); their bare
backups hold the full history. Clone or `git subtree pull` from the bare
backups when the full history is needed.

Re-check at any time with the loop in §6.1. A repository that reports
`DIFFERS` needs the push from §4.4; nothing else has to be done to the
others, which answer `Everything up-to-date`.

---

## 11. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `fatal: this operation must be run in a work tree` | you are running `checkout`/`status` inside the bare repo | clone it, then work in the clone |
| `src refspec align/smp-upstream does not match any` | the branch is not named that in this checkout | `git branch --show-current`, then push that name |
| `! [rejected] … (non-fast-forward)` | the backup has commits you do not | `git fetch local`, compare, rebase/merge, push again |
| `! [rejected] … (fetch first)` | stale remote-tracking ref | `git fetch local` then retry |
| push succeeds but GitHub did not change | you pushed to `local` only | push to `origin` too |
| clone of the bare repo is empty | wrong path, or the bare repo was never pushed to | `git --git-dir=<bare> for-each-ref` |
| `Everything up-to-date` when you expected a push | HEAD already matches the bare branch | `git log --oneline -1`, `git ls-remote local` |
| the build cannot find a port after a restore | the sibling layout or the xpm links are missing | section 3.5 |
| `shallow roots are not allowed to be updated` | fetching from a shallow working repository (kernel, Cortex-M, POSIX) into a full one | fetch from its bare backup instead |
| `Cannot lock ref` / `failed to update ref` | two pushes at once, or a stale lock file | retry; if it persists, inspect `<bare>/refs/…` and `<bare>/*.lock` |
