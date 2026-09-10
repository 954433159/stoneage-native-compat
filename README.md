# Stoneage Native Compatibility

将指定石器客户端连接至独立原生 GMSV/SAAC 的兼容工程。原生战斗、NPC、物品、宠物及角色保存由服务端执行；账号接口使用 Python。

**发布边界：服务端依赖已有的参考 C 源码；客户端使用经过哈希校验的原 EXE 和可重放的 Lua/资源补丁。这里不是完整客户端反编译源码，不能用本仓库重新编译原客户端 EXE。**

本公开源码包不包含账号、密码、数据库记录、角色、截图、聊天记录或原工程历史。第三方原始客户端、地图、美术、参考服务器源码需要另行合法取得；其再分发许可尚未核实，不承诺只克隆本仓库就拥有完整游戏资源。

## 环境

已验证环境为 Windows 11 + WSL Ubuntu 24.04、GCC 13、CMake 3.28、MariaDB 10.11、Python 3.12。Python最低要求为3.11。运行默认只监听本机；不会复用已有系统 MariaDB，也不需要开放数据库到局域网。

在 Ubuntu/WSL 内安装依赖：

```bash
git clone https://github.com/954433159/stoneage-native-compat.git
cd stoneage-native-compat
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libmariadb-dev zlib1g-dev mariadb-server mariadb-client python3 python3-venv
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

依赖安装属于系统包管理操作，请确认使用你自己的 Ubuntu 发行版。

## 从干净源码准备实例

准备两个**独立、未经修改**的原始输入目录。`release-input-lock.json` 锁定具体文件哈希，版本不同会明确失败，不会关闭校验。

```text
reference-source/
  sq_server_game/175Gmsv/GameServer/   # 原始 .vcxproj、C、头文件
  sq_server_game/projects.zip         # 原版资源归档
  sq_server_game/175sa.sql            # 只读取表结构，不导入历史记录
  sq_server_acc/175Server/            # 原始 SAAC 工程
  Stoneage_80/lua/lua/                # 目标6021 Lua资源编译器源码
original-client/
  stoneage.exe
  data/ lua/ map/ path/ 及原配 DLL
```

在新克隆的仓库目录执行（路径换成自己的）：

```bash
.venv/bin/python tools/bootstrap_release.py \
  --reference-source /mnt/c/stoneage-inputs/reference-source \
  --original-client /mnt/c/stoneage-inputs/original-client
```

脚本会核对输入、生成原生补丁、重建 GMSV/SAAC、编译两套 Lua 检查工具、准备客户端兼容资源并验证结果。它拒绝覆盖已经存在的实例。首次建立的是**新的空玩家实例**，不是开发者存档的复制；配置密码随机生成，不提供通用默认账号。

## 启动和停止

Windows 双击 `start-local.cmd`，或 PowerShell 执行：

```powershell
.\local-game.ps1 -Action start
.\local-game.ps1 -Action status
.\local-game.ps1 -Action stop
```

启动会等待原生地图和NPC加载完成再打开客户端，冷启动可能需要数分钟。在客户端注册自己的账号、创建角色。`stop-local.cmd` 正常关闭本实例客户端并保存停止服务，不强杀其他游戏或数据库。

默认端口：HTTP 19090、GMSV 29065、SAAC 21003、MariaDB 23306。端口被其他实例占用时会拒绝启动；不要同时运行两个默认端口实例。Windows入口默认使用名为 `Ubuntu` 的WSL发行版。

Linux服务端可以依次运行 `.venv/bin/python tools/runtime.py database`、`native`、`http`、`ready`，停止用 `.venv/bin/python tools/runtime.py stop`；原游戏客户端仍需Windows。已以Ubuntu普通用户验证本项目独立MariaDB目录的初始化和启动。

配置由准备脚本生成。若手动修改 `.cf` 文件，请保留UTF-8及LF换行；原生配置解析器会将CRLF中的回车读入地址等字段。客户端HTTP地址当前固定为 `127.0.0.1:19090`，改HTTP端口还需要同步生成并核验客户端补丁，不能只改服务端配置。

## 检查与备份

```bash
.venv/bin/python tools/verify_prepared.py
.venv/bin/python tools/run_native_checks.py
.venv/bin/python tools/run_extended_checks.py
```

市场/仓库真实数据库回归只运行专用隔离工具：

```bash
sudo .venv/bin/python tools/run_feature_database_checks.py market
sudo .venv/bin/python tools/run_feature_database_checks.py storage
```

它们创建随机隔离库和受限临时用户。不要将测试SQL夹具导入玩家库。

备份时先正常退出客户端，再运行 `tools/runtime.py maintenance` 停止HTTP/GMSV/SAAC并保持本项目MariaDB，之后以本机数据库管理员执行 `tools/backup_instance.py`。必须等备份退出成功且manifest核对通过才能重新启动。Git不备份玩家资产。

## 已验证与尚未完成

开发实例已验证五控切换、物品/宠物仓库、原生商城和交易等多项流程。最近增加：三种骑乘预览匹配、红狗实际骑乘及重登、贝壳和成人任务完整流程及冷重登。自动回归、独立数据库测试、原客户端实机操作分别记录，不能相互替代。

四种骑乘仍缺目标资源，2D骑宠本地资格未接入；部分活动奖励、外服副本和高级任务未完整实现。任务目录包含2.5内容，不代表整版任务已全部通关。公开构建的验证结果见 `docs/RELEASE_VALIDATION.md`。

## 修改方式

原生改动维护于 `native/patches.json` 和独立 C 模块，Lua资源维护于 `native/resource-patches.json` 和对应模块；`native/overrides` 是生成目录。保留原始文件并重新生成、校验，不手改生成物。

详细来源与许可边界见 `THIRD_PARTY.md`。公开发布前运行 `tools/scan_public_release.py --git-index`；扫描通过不能代替人工审查或第三方授权。
