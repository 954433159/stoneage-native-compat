# 锁定源码的只读校验与筛选打包

这个工具只在**已经存放源码的那台机器**上运行。云端构建机访问不到 `D:\sqsd`，也不能绕过 CopyToBox 的允许根目录。本文件没有声称已经在那台机器上执行过打包。

## 已对照的 5 个标记

对照公开的 `release-input-lock.json`，下面 5 个哈希全部一致。这只证明这 5 个文件与锁定快照相同，不能代替其余 559 个参考文件的校验。

| 相对路径 | 结果 |
| --- | --- |
| `sq_server_game/175Gmsv/GameServer/GameServer.vcxproj` | 一致 |
| `sq_server_acc/175Server/AccServer.vcxproj` | 一致 |
| `sq_server_game/175sa.sql` | 一致 |
| `sq_server_game/projects.zip` | 一致 |
| `Stoneage_80/lua/lua/VMProtectSDK.h` | 一致 |

`E:\BaiduNetdiskDownload\stoneage\stoneage.exe` 与 `stoneage.exe.bak` 的哈希都是锁定原版 `916709b78df68d276c2c025f1b30d83dc5f5eaa6d0cbb7624da7b0b422623cd7`。`stoneage.local.exe` 和 `stoneage_local.exe` 的哈希不同，而且不在锁定清单里，不能当作这份构建的原始客户端。

目录名 `out` 不在锁定清单中。整棵目录有 19396 个文件，锁定参考输入只有 564 个。不要按目录名判断其余文件的完整性或许可。

## 最小构建范围

就地完整构建需要锁定清单中的全部 564 个参考文件，外加客户端锁定树。筛选打包故意少拿 3 个文件，因此**打包结果本身不能启动游戏**。

必须留在原目录、由 `tools/bootstrap_release.py` 就地读取的文件：

- `sq_server_game/175Gmsv/GameServer/`：474 个锁定文件，其中 `char/ls2data.dat` 不打包
- `sq_server_acc/175Server/`：33 个锁定文件
- `Stoneage_80/lua/lua/`：55 个锁定文件
- `sq_server_game/175sa.sql`：哈希已一致，但是转储，不打包
- `sq_server_game/projects.zip`：哈希已一致，但是资源归档，不打包

客户端若要准备可运行副本，使用 `stoneage.exe`（或与它哈希相同的 `.bak`）以及锁定清单里的 `data/`、`lua/`、`map/`、`path/` 和根目录 DLL。这些是客户端素材，本工具不打包、不上传。

## 工具做什么

`tools/package_locked_sources.py`：

1. 只读 `release-input-lock.json`。
2. `--explain` 只打印上述布局，不碰源码磁盘。
3. `--source-root` 对 564 个相对路径逐个做 SHA-256。不跟随符号链接，不写源目录。
4. 有任一缺失或哈希不一致，退出码为 2，不创建输出。
5. 全部一致而且提供了新的 `--output` 时，才复制 561 个 `.c`、`.h`、`.hpp`、`.vcxproj`。
6. 不复制 `.sql`、`.zip`、`.dat`。
7. 拒绝把输出写进源码树内部，也拒绝写进本仓库目录，避免私有源码进入公开 Git。
8. 复制时再次核对哈希。源文件内容在工具运行后应保持不变。

先只校验：

```bash
python tools/package_locked_sources.py --explain
python tools/package_locked_sources.py --source-root D:\sqsd\第一期修复的175源码 --check-only
```

校验通过后，输出到一个**新的、允许传输的空目录**。不要把输出选在本仓库里：

```bash
python tools/package_locked_sources.py --source-root D:\sqsd\第一期修复的175源码 --output E:\stoneage-locked-sources
```

可选地只核对一个客户端文件，不复制它：

```bash
python tools/package_locked_sources.py --client-exe E:\BaiduNetdiskDownload\stoneage\stoneage.exe
```

`--explain` 已在云端运行，确认锁定清单是 564 项、可打包 561 项、排除 3 项。`--source-root` 没有在云端运行，因为这里没有 D 盘。
