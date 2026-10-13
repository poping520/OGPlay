# BND-54：压缩纹理能力策略

状态：实现及 macOS 定向验证完成；Windows/Linux 与正式 title gate 未验收。依赖：现有 ATC/PVRTC/ETC1 回退、Context 生命周期及 Manifest 事实。
设计：[ADR-0128](../../adr/media.md#adr-0128)。

目标：根据明确的 APK 纹理声明选择强类型格式族，统一 guest 查询与上传，修复格式选择冲突。
范围：all/atc/pvrtc、ETC1/ES3 核心格式保留、native/Java/GLES1/2/3 查询、2D/3D/PBO 上传、
Context 继承与重建、顶层 supports-gl-texture 解析及 native 初始化前的选择。
无新解码算法、GPU 身份模拟或 guest 补丁；无/未知/歧义声明保持默认。

验收：无 Profile 的声明路径选择一致；默认同时发布厂商族时重现消费者选择冲突；
查询/数量/长度/上传一致，隐藏格式明确失败；
共享/新建 Context 与进程策略隔离；无/未知/歧义声明保持默认；PVRTC 回退定向复用。
原 APK 同路径验证纹理恢复、无 guest fault 并正常退出。仅定向检查，不作完整 title gate。

验证：Release ogplay/ogplay_tests 构建通过；移除 Profile 设置后 17 个定向用例/2953 断言通过，覆盖声明
解析（UTF-8/16、去重/未知保留、嵌套忽略及非法名称）、无 Profile 的真实进程装配、
歧义/未知回退、查询/CPU/PBO 上传、Context/Java、核心格式及 ATC/PVRTC 回归。
Profile validator self-test/current、quirk registry current 三项通过；boundary_hot_path 在
压缩能力底座实现时通过，声明接入未改变该热边界。只运行相关定向检查。

Lili 的 Profile 已删除，原 APK 直接运行（增加 MCP/diag 取证）；日志 profile=none，
family=atc source=manifest，声明来自二进制 Manifest 的 GL_AMD_compressed_ATC_texture。
角色/建筑/天空纹理恢复，guest_fault=null、gl_errors=0；MCP shutdown presented=3536、退出 0。
Amazon Appstore required 提示仍在，未扩大到按钮、音频或下一独立问题。
原 APK 运行证据 `.local/lili-manifest-texture-20261013/`；Profile 设置及其解析、schema、
注册和启动注入均已移除，构建/回归证据 `.local/lili-remove-profile-setting-20261013/`。
早期 Profile 验证仅留历史证据
`.local/lili-texture-fix-20261013/`，不再是最终实现。没有生产 guest PC 探针或 APK/SO 补丁。
