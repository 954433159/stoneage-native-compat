# 公开仓库检索（2026-11-08）

这次没有读取 D 盘，也没有把任何第三方源码或资源克隆进本仓库。下面的结论来自 GitHub 仓库说明、根目录、许可证文件和少量公开配置。没有在云端编译这些仓库，因此没有新的试玩地址。

当前安卓客户端是 `versionName=20260805` 的 SDL 程序，账号走 `25server` HTTP，游戏端口来自服务器列表，本仓库默认游戏端口是 29065。能对上这套契约的公开完整源码没有找到。

## 最接近的五个仓库

### 1. https://github.com/dongfangzan/stoneage-2.5-docker

最近提交 2026-10-26。根目录有 `gmsv`、`saac`、数据库初始化、Docker 构建和作者写的验证记录。作者称 2026-10-26 在 macOS 上从零编译，saac/gmsv 握手成功，监听 `9065`，载入 2135 个 NPC 和 706 张地图。云端没有重复这次构建。

许可证原文：

- 根目录 `LICENSE` 是 MIT，版权标成 2026 stoneage-2.5-docker contributors。
- 根目录 `NOTICE.md` 写明 MIT 只覆盖新增的 Dockerfile、Compose、脚本和文档。
- 同一份 NOTICE 写明 `server-2.5/` 的 C 源码和 DATA 来自 chenmingbiao/stone-age，上游没有许可证，血统是 longzoro 2006 年的 2.5 商业代码，版权仍归原权利方。

游戏服、账号服和资源在该仓库里可见。客户端不在仓库里，说明要求另下华义 2.5 的 `sa_2903.exe`。公开 `setup.cf` 是 `port=9065`、`acservport=9300`，没有 `25server` HTTP 登录。与 20260805 安卓客户端和锁定的 Windows `stoneage.exe` 不是同一代协议，不能直接替换。

### 2. https://github.com/chenmingbiao/stone-age

最近提交 2016-03-25。根目录同时有 `gmsv/` 和 `saac/`。`gmsv` 含 `lssproto_serv.c`、`makefile`、`data/` 和 `data.zip`；`saac` 含 `saacproto_serv.c`、`makefile` 以及一个已编译的 `saacjt20.exe`。没有 Lua 目录，没有客户端。全树没有 LICENSE、COPYING 或 NOTICE。README 只说供研究服务端技术、不涉及商业盈利，这不是使用许可。

它是上一仓库声明的上游。协议代际同样是旧 2.5，不是 20260805 的 HTTP 账号加 App 登录。

### 3. https://github.com/gavinlinasd/StoneAge

API 返回的最新提交是 2018-08-21。README 原文写 “StoneAge 8.0 source code I found online.” 根目录有 `gmsv/` 和 `saac/`，两边都有 `src/`、配置和数据目录。没有许可证文件。作者自己说明来源是网上找到的代码，不能把它当成已授权的完整实现。没有证据表明它实现 20260805 的 `25server` 登录。

### 4. https://github.com/OpenGameSys/stoneage-web

最近提交 2026-11-07，仍在修战斗问题。仓库里有 Go 服务端、TypeScript/WebGL 客户端、`proto/messages.json`、`gamedata/` 和约 0.9 GB 的 `assets/`。README 写 1370 张地图、2156 个 NPC，并给出从源码构建的命令。14499 个文件里没有 LICENSE、COPYING 或 NOTICE。README 里的“完整代码完全开放”不是许可证文本。

README 同时写明：不兼容原版线路协议，也不兼容原版 Windows 客户端。协议是 WebSocket 加 JSON。它是另一套网页游戏，不是 175/20260805 客户端的服务端。这里不把它换成本仓库的目标。

### 5. https://github.com/naka507/stone-age-server

最近提交 2026-10-18。C++20 工程，有 `src/saac`、`src/world`、IDL 和很长的设计文档，没有独立的原版客户端、Lua 脚本目录或原版地图包。399 个文件里没有许可证。

`docs/02-protocol.md` 写明：“R2 已定不实现旧 lssproto。新协议不与任何旧客户端互通。” 因此它和 20260805 安卓客户端、锁定的 Windows 客户端都不兼容。文档还写阶段进度和测试通过不代表原版玩法已经还原。

## 看过但不是这条客户端的完整源码

- https://github.com/iriselia/StoneAge ：2019-12-17，有 `gmsv` 和 `saac` 的 lssproto 源码，488 个文件里没有 `data/`，也没有许可证。资源不齐。
- https://github.com/librae8226/gmsv ：2012-05-04，只有 gmsv，没有 saac，没有许可证。
- https://github.com/alrightlook/StoneAgeMobileApp ：2018-04-19。`android-project/jni` 里有 `lssproto_cli.cpp` 和 `login.cpp`。抽查的 `login.cpp` 没有 `http://`、`25server` 或 `AppClient`，版本号宏是 `SA_VERSION_NUM 300`。根目录 `COPYING.txt` 是 SDL 的 zlib 声明，不覆盖游戏客户端。不能当作 20260805 客户端。
- https://github.com/anson1788/stoneage ：服务端以 `Serv.zip` 形式存放，另有 8.5 客户端 Visual Studio 工程和 Lua 目录。说明文本要求 CentOS 32 位，没有许可证。
- https://github.com/asteedma/stoneage-switch-nas ：Switch 容器部署，跑的是原版程序镜像，不是可独立许可的源码；客户端配置是 Switch 的 `serverlist.json`。
- https://github.com/MCVoxelSH/Stone-Age-2-Private-Server ：另一款 Stone Age 2，作者已停更。
- https://github.com/freeznet/GMSVrelease ：README 写的是 CrossGate（魔力宝贝），不是石器时代。

## 现在能做和不能做的

没有一份许可明确、并且能接 20260805 SDL 客户端的完整 GMSV/SAAC/Lua/资源组合。所以没有把上述源码并入本仓库，也没有用它们启动服务。

本仓库已有的公开部分仍然有效：账号 HTTP、服务器列表和更新地址分离，以及只读的 564 项校验工具。这些不构成游戏 TCP 握手、建角、进图、移动、战斗或热更新闭环。
