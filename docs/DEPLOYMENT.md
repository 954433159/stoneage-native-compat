# 部署与 20260805 客户端兼容边界

本文记录这次在公开源码上实际构建和测试的结果。没有访问分析文本里的第三方服务器，没有安装或运行 APK，也没有把 APK、证书、本地源码或游戏素材放进仓库。

源码机回报的 5 个标记哈希已与 `release-input-lock.json` 一致，包括两个工程文件、`175sa.sql`、`projects.zip` 和 `VMProtectSDK.h`。`stoneage.exe` 与 `stoneage.exe.bak` 是锁定原版；两个 local EXE 不是。这 5 项不能代替其余 559 个参考文件。只读校验工具和排除规则见 `docs/SOURCE_PACKAGE.md`。云端没有执行 `--source-root`，因为访问不到该磁盘。逐项玩法状态见 `docs/ACCEPTANCE.md`。

## 客户端身份

授权的静态分析文本说明，已上传的 APK 是 `com.cwjhs.stoneage.ko`，显示名「重返華義石器」，`versionName=20260805`，`versionCode=1`。ABI 为 `armeabi-v7a` 和 `arm64-v8a`，主库是 `libStoneage.so`，并加载 SDL2。它是安卓原生客户端，不是 Win11 可执行文件，不是网页，也不是远程桌面。

包内只有字体和少量皮肤，完整地图、精灵、音效和 Lua 需要热更新。服务器列表、客户端版本检查和资源清单是三条不同的原生字符串：

- `http://202.189.23.53:90/25server/serverlist.php`
- `http://202.189.23.53:90/25server/clientupdate.php`
- `http://202.189.23.53:90/25server/update/listnew.php`

另外还有资源前缀 `http://shiqiuu.test.upcdn.net/`，以及 Java 侧字符串 `http://www.juuding.com/182server/update/update.json`。分析没有证明后两条在运行时一定被请求。它们是静态字符串。端口 90 属于这些 HTTP URL，分析没有给出游戏 TCP 地址。随包没有可直接改的游戏端点配置。存在 v1 和 v2 签名，没有 v3。改原生库会破坏原签名，需要有权维护者重新签名；本仓库不修改 APK。

本仓库的登录补丁已经按目标版本 `20260805` 写了应用签名常数。Windows 准备脚本原来只替换 `http://202.189.23.53:90`。对这条客户端，只替换这一处会留下资源前缀和 Java 更新字符串。

## 服务端现在怎么区分地址

账号注册和登录只由本机 `account_service.py` 处理，响应里没有跳转到原站。服务器列表的 JSON 顺序仍是 `name/list` 和 `name/ip/port/type`。默认游戏地址是 `127.0.0.1:29065`。这个端口来自本实例配置，不是 URL 里的 90。配置成 90，或把游戏端口配成 HTTP 端口，都会被拒绝。原站主机也不能被写进游戏地址或更新清单。

`STONEAGE_GAME_HOST` 和 `STONEAGE_HTTP_HOST` 可以临时覆盖设置。手机访问时，HTTP 绑定地址、列表里的游戏地址，以及 GMSV 的 `STONEAGE_BIND_ADDR` 必须是手机路由得到的同一个 IPv4 地址。任一端仍是 `127.0.0.1` 时，`/health` 的 `same_machine_only` 为真。未设置或非法的绑定地址继续只听回环，SAAC 仍只听回环。

更新清单默认是空数组，因此不会下发原站补丁。若显式提供 `update_document`，其中的绝对 URL 只能指向本实例 HTTP 源或单独配置的 `update_origin`。指向原站、资源前缀、Java 更新地址或游戏 TCP 端口的清单会返回 503，且响应体不带回那些地址。`clientupdate.php` 仍返回单个字节 `1`，这是本仓库已经锁定的 SDL 契约；这次 APK 分析没有反汇编该判断，所以不能把它重新解释成“已经证明安卓不会更新”。

`tools/prepare_client.py` 会同时改写账号、列表和上述更新通道。替换后的 URL 必须放进原来的 C 字符串槽。`http://192.168.1.20:19090` 比原来的 24 字节源站前缀长，准备过程会整单拒绝，不会只改其中一条。UTF-16LE 的账号或更新地址也会整单拒绝。微信、统计或其他未分类地址不会被改写，也不会被拿来登录。

## 这次的构建和测试

环境是本构建机的 Ubuntu，GCC/Clang 可用，CMake 3.28，MariaDB 客户端库 3.3.17，Python 3.12。

| 项目 | 结果 |
| --- | --- |
| `native/bind_address.c` | 编译并通过回环默认、拒绝 `0.0.0.0`、接受单个 IPv4 单播 |
| CMake 配置 GMSV/SAAC | 找到 libmariadb 和 zlib。`native/overrides` 下没有专有源文件，配置失败，没有生成 `gmsv` 或 `saac` |
| 端点改写单测 | 通过。只替换 90 端口主机时，资源前缀和 Java 更新主机仍在 |
| 服务器列表、端口 90、外部更新清单 | 通过 |
| HTTP 路由 | 通过。列表返回 `192.0.2.10:29065`；版本检查返回 `1` 且无跳转；外部资源 URL 被拒绝 |
| MariaDB 账号契约 | 既有 `tests/test_accounts.py` 11 项通过，使用本次临时库和只覆盖本服务查询的空表。包含注册、错误密码、令牌轮换、5 个小号和角色预览 |
| 游戏 TCP 握手、进图、战斗 | 没有做。GMSV 没有编出来 |
| Win11 真机、安卓真机 | 没有做。APK 未安装、未运行、未改签 |

临时 MariaDB 只在这台构建机上，测完不作为对外服务。没有公网试玩地址，也不能把这次测试称为架设完成。

## 协议对照到哪一步

APK 主库有 171 个 `lssproto_*` 符号，说明它属于石器 lssproto 家族。公开仓库里的 `lssproto_serv.c` 不在树中，所以不能用“公开补丁没写到某个函数”推出原版 GMSV 没有该消息。

能和现有代码对上的部分：

- 版本字符串 `20260805` 与登录补丁一致。`tests/login_platform.lua` 接受 Win、Android、iOS 的协议 2，并拒绝 Android 走旧 PC 协议 1。这是源码级策略测试，不是这次拿到的 APK 实机登录。
- 客户端符号 `AppClientLogin_send(int, char*, char*, char*, char*, char*, int, unsigned int, char*)` 和 `ClientLogin_recv(int, char*)` 与本服务端的 App 登录方向一致。参数顺序和 MD5 握手没有在这次分析中重放。
- `WN_recv` 在连接号之后是 4 个整数加一个字符串，与 `app_mail.c` 的 `WN_send` 调用元数一致。
- `PETST_send/recv` 和 `KS_send/recv` 的元数与现有调用一致。`PETST` 的 0/1 待机语义仍由既有 `tests/pet_state.c` 覆盖，不是新的实机验证。
- `I_recv` 和 `S_recv` 都是一个字符串。21 和 22 个物品字段在字符串内部；符号只能证明有这个字符串参数。

还不能当成兼容的部分：

- 聊天测试把 App 档案编码成 `ISISSII` 七个线字段。APK 的 `TK_recv` 在连接号之后只有六个 C++ 参数。符号元数不能证明线格式。
- `1ji5*`、`redMoney`、`SaMenu`、`NewLogout` 这些客户端符号没有被本仓库的公开补丁点名。`NEWLOGOUT2`、`NewSaMenu`、`WNUpdate`、`PartyHigh` 是服务端使用的名字，不能只凭大小写或名字不同宣布线编号不一致。
- GBK 线格式和 UTF-8 内部数据是既有 SDL 补丁的结论。这次分析没有给出 APK 里的转换函数。
- 没有消息编号表，没有密钥派生重放，没有校验和样本。

## 参考仓库

GitHub 用户 `954433159` 的公开仓库共 20 个，其中包含 fork。与石器时代直接相关的只有本仓库 `stoneage-native-compat`。其余公开 fork 是其他项目。没有可见的 Gist，也看不到私有仓库。

下列公开仓库没有 SPDX 许可证，或许可证为 `NOASSERTION`。它们没有被复制进本仓库，也不构成可分发的服务端：

- <https://github.com/librae8226/gmsv>
- <https://github.com/chenmingbiao/stone-age>
- <https://github.com/gavinlinasd/StoneAge>
- <https://github.com/alrightlook/StoneAgeMobileApp>
- <https://github.com/freeznet/GMSVrelease>
- <https://github.com/anson1788/stoneage>

## 目录名不能当成源码已经对齐

本机 `D:\sqsd` 下看到过这些目录名：`WGCODE轻石器客户端服务端源码`、`服务端sa`、`EE老版U3D源码`、`Unity版EE前端源码_1788757144748`、`EE新前端`、`EEsuoyou`、`EE备份`、`175php代码`、`175端UI半成品（自制娱乐）`、`第一期修复的175源码`，以及 `data` 和 `stoneage.apk`（21464283 字节）。只看名称不能判断里面是否完整，也不能认为它们有开源许可。Unity/EE 前端是否使用同一套 lssproto，目前没有证据。这些文件都没有复制到本仓库。

## 还需要主对话在允许的位置读取的最小材料

不要把这些文件提交到公开仓库，也不要访问原站。

1. 只回报下列标记文件的 SHA-256，用来判断本机目录是不是本仓库锁定的那一套输入。不要上传文件本体：
   - `sq_server_game/175Gmsv/GameServer/GameServer.vcxproj`
   - `sq_server_acc/175Server/AccServer.vcxproj`
   - `sq_server_game/175sa.sql`
   - `sq_server_game/projects.zip`
   - `Stoneage_80/lua/lua/VMProtectSDK.h`
2. 只有标记哈希与 `release-input-lock.json` 一致时，才在私有目录保留全部 564 个锁定文件并运行 `tools/bootstrap_release.py`。哈希不一致就停，不要把“修复版”源码推上来。
3. Windows 客户端四个 EXE 的 SHA-256。锁定的 `stoneage.exe` 为 `916709b78df68d276c2c025f1b30d83dc5f5eaa6d0cbb7624da7b0b422623cd7`，此前看到的大小是 7669248 字节。另外只需 `saCN.ini` 和两个 704 字节 `.manifest.json` 的脱敏文本。
4. 若本机已经有保存下来的 `listnew.php` 响应、`data/update/list.dat` 或 `Patch_*.ini`，去掉账号、令牌和设备标识后提供文本。这是确认资源清单格式的最小样本。没有样本时，服务端继续返回空清单，避免第三方补丁覆盖本地修改。
5. 不需要再传 APK，不需要 EE/Unity 整树，也不需要把 `data` 美术目录放进 Git。
