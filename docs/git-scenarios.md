# Git 场景速查

> 本文件不是 Git 完整教程，只是这个项目中最常遇到的四类场景。
> 真正用的时候，可以拿本文件当速查卡。

---

## 场景 1：撤销某次提交

### 撤销最近一次提交（保留文件变更）

```bash
git reset --soft HEAD~1
```

- 提交撤销了，但所有文件变更还保留在**暂存区**（`git status` 能看见）
- 适合：提交信息写错了，想改完重新提交

### 撤销最近一次提交（连变更也扔掉）

```bash
git reset --hard HEAD~1
```

- ⚠️ **危险**：工作区和暂存区的所有变更都会被清除
- 适合：提交完全错了，连代码都不想保留
- 如果已经 `push` 到远程，需要额外一步 `git push --force`，**慎用**

### 已经 push 到远程了怎么办

```bash
git reset --hard HEAD~1
git push --force
```

- ⚠️ `--force` 会覆盖远程历史，如果有人已经基于远程提交工作了，会造成混乱
- 本项目目前只有你一个人操作，风险较低，但仍要谨慎

### 撤销的不是最近一次（中间隔了几个提交）

```bash
git log --oneline          # 找到要撤销的那次提交的 hash
git revert <commit-hash>
```

- `revert` 不会删除历史，而是生成一次**反向提交**（把那次提交的变更再改回去）
- 适合：已经 push 到远程的情况，因为不进历史篡改

---

## 场景 2：开分支做实验

### 建分支 + 切换过去

```bash
git branch day08-experiment    # 创建新分支（从当前 HEAD 分叉）
git checkout day08-experiment  # 切换到新分支
```

或者一步到位：

```bash
git checkout -b day08-experiment
```

### 在当前分支正常开发，实验完了怎么办

**实验成功** → 把实验分支合并回来：
```bash
git checkout main
git merge day08-experiment    # 把实验分支的变更合并到 main
```

**实验失败** → 直接删掉分支：
```bash
git checkout main
git branch -D day08-experiment   # 强制删除（即使没合并也能删）
```

### 推分支到远程

```bash
git push origin day08-experiment
```

---

## 场景 3：改错了想恢复

### 只改了一个文件，想恢复成上次提交的样子

```bash
git restore <文件名>
```

例如：
```bash
git restore qiu_07_day/bootpack.c
```

### 把整个工作区恢复成上次提交的样子

```bash
git restore .
```

- ⚠️ 所有没有 `git add` 的修改都会丢失

### 已经 git add 了，想取消暂存

```bash
git restore --staged <文件名>
```

### 想回到过去某个提交查看状态（但不想改历史）

```bash
git log --oneline                # 找 commit hash
git checkout <commit-hash>       # 进入"游离 HEAD"状态，只读
```

```bash
git checkout main                # 回到最新状态
```

---

## 场景 4：多人协作

> 本项目目前只有你一个人操作，以下内容先了解即可，等真需要时再查。

### 拉取别人的更新

```bash
git pull
```

等价于：
```bash
git fetch          # 下载远程的更新
git merge          # 合并到当前分支
```

### 产生冲突时的流程

```
你的修改  vs  别人的修改  →  冲突
```

1. `git status` 看哪些文件有冲突
2. 打开冲突文件，找到 `<<<<<<`、`======`、`>>>>>>` 标记
3. 手动决定保留谁的代码，删掉标记
4. `git add <冲突文件>`
5. `git commit`（不需要 `-m`，Git 会给你一个默认的 merge 信息）

### merge 和 rebase 的区别（一句话版）

- **merge**：把两个分支的终点连起来，保留完整历史分叉，安全
- **rebase**：把一条分支的历史"嫁接"到另一条后面，历史是线性的，但改变了提交

> 一个项目规则：**永远不要 rebase 已经 push 的分支**。只在你自己的本地分支上 rebase。

---

## 本项目铁律（重申）

| 规则 | 说明 |
|------|------|
| 提交信息由用户写 | AI 不自动生成任何 commit message、tag message |
| 标签格式 `v{天数}{后缀}` | 详见 `docs/tag-convention.md` |
| push 前询问用户 | 每次 git 操作前确认 |
| 不 force push | 除非用户明确要求 |
