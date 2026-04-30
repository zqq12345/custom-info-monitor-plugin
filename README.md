自定义监控插件 使用说明

概述

本插件用于通过HTTP请求获取多个服务器的自定义数据（如温度、运行状态等），并在TrafficMonitor任务栏悬浮窗中原样显示服务器返回内容。支持多URL配置，自动过滤无效地址，无配置时完全静默运行。

功能特性

特性 说明

✅ 多URL轮询 支持配置多个HTTP地址，每次请求自动切换下一个

✅ 纯文本显示 服务器返回什么内容，悬浮窗就显示什么（不做任何格式化）

✅ 智能过滤 自动忽略不符合URL格式的配置行（如无效协议、空行）

✅ 静默运行 无配置时不弹窗、不干扰系统任务栏

✅ 编码兼容 支持UTF-8/GBK编码的服务器响应

安装与配置

1. 文件放置

将编译好的 CustomMonitor.dll 放入TrafficMonitor插件目录（如 TrafficMonitor\plugins\）。

2. 配置文件

在插件DLL同目录下创建 custom_info.ini，格式如下：
# 每行一个URL（注释行以#开头，会被自动忽略）
http://192.168.3.10:18080/sensors
http://192.168.3.1:18080/osx
https://example.com/status

• ✅ 有效URL：必须以 http:// 或 https:// 开头

• ❌ 无效URL：不含协议前缀或格式错误的行会被自动跳过

• 📝 空文件支持：无配置时插件完全静默，不占用资源

使用方法

1. 启用插件

打开TrafficMonitor → 插件管理 → 勾选「自定义监控插件」→ 重启TrafficMonitor生效。

2. 查看数据

场景 显示效果

悬浮窗 鼠标悬停在任务栏插件区域，显示当前URL的返回内容（无配置时不显示）

设置窗口 右键点击插件 → 选项，查看所有配置的URL及响应状态（未获取/具体内容）

编译指南（开发者）

环境要求

• MinGW-w64（推荐WinLibs版本）

• TrafficMonitor插件开发包（含 PluginInterface.h）

编译命令

@echo off
set PATH=D:\soft\mingw64\bin;%PATH%
g++ -shared CustomMonitor.cpp -Iinclude -lwinhttp -static -static-libgcc -static-libstdc++ -municode -o CustomMonitor.dll
pause

• ✅ -municode：启用Unicode支持，解决中文乱码问题

• ✅ 静态链接：避免依赖系统运行时库

常见问题

Q1：悬浮窗显示乱码？

• 检查服务器响应编码：若为UTF-8，确保编译时添加 -municode；若为GBK，无需额外参数

• 确认源代码文件编码为ANSI（GBK），避免UTF-8无BOM导致的解析错误

Q2：设置窗口显示“未获取”？

• 直接用浏览器访问URL，确认能返回纯文本内容

• 检查服务器是否返回HTTP错误（如404、500）

• 查看插件目录下的 debug_raw.txt（若有），确认原始响应内容

Q3：多URL只显示一个？

• 插件采用轮询机制：每次DataRequired触发时切换下一个URL，多URL会轮流显示

• 若需同时显示所有内容，可修改GetTooltipInfo()函数拼接所有响应

注意事项

⚠️ 配置文件路径：必须放在插件DLL同目录，而非TrafficMonitor根目录  
⚠️ 请求频率：默认每5秒轮询一次，可通过修改DataRequired()中的时间间隔调整  
⚠️ 响应格式：服务器需返回纯文本（JSON、HTML等内容会原样显示）

更新日志

版本 日期 说明

v1.0.0 2026-04-30 初始版本，支持多URL轮询、纯文本显示

文档维护者：zqq12345 | 最后更新：2026-04-30
