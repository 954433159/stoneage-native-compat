# 来源与再分发边界

本仓库维护兼容模块、可重放补丁、构建工具和测试。参考原生服务器、目标客户端EXE及美术/地图资源来自另行提供的第三方输入，未找到覆盖整套游戏的明确公开再分发许可。

- 原始客户端目标SHA-256：`916709b78df68d276c2c025f1b30d83dc5f5eaa6d0cbb7624da7b0b422623cd7`。
- 原生基础工程为独立GMSV/SAAC；Lua资源编译器来源为参考工程中的6021格式实现。这不等于原客户端完整源码已恢复。
- `native/patches.json` / `native/resource-patches.json` 的匹配上下文用于说明兼容变更；第三方片段保留其原权利归属，不将其宣称为新创作或重新授予MIT许可。
- 宠物成长/骑乘目录仅保留游戏中公开显示的种类、范围、技能和图号，不包含排行榜玩家身份、账号或会话数据。参数拟合与实际已验证值在原生配置中区分。
- 原生基础源码、EXE、DLL、游戏美术、地图、字体及音频不在本源码仓库随附。下载/运行它们需要使用者自行取得相应权利；脱敏不等于取得再分发授权。

只有项目独立贡献适用根目录许可；第三方代码及素材的许可不能由本项目代为授予。如后续发布整套资源包，必须在发布清单中逐项注明来源、授权与脱敏范围。

公开检索到的这些仓库没有可接受的 SPDX 许可证，或许可证为 `NOASSERTION`，因此没有把它们的源码复制进本仓库：<https://github.com/librae8226/gmsv>、<https://github.com/chenmingbiao/stone-age>、<https://github.com/gavinlinasd/StoneAge>、<https://github.com/alrightlook/StoneAgeMobileApp>、<https://github.com/freeznet/GMSVrelease>、<https://github.com/anson1788/stoneage>。用户本机上的目录名称也不构成再分发授权；那些源码、APK 和素材没有进入本仓库。

GitHub官方说明：[仓库许可](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository)、[大文件与Release附件](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)。
