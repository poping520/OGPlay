# 子模块：loader/resources

不依赖 VM 的 API19 ARSC 表、字符串池与配置选择。Loader 的 Manifest、游戏库和运行时资源核心共用这里的唯一表解析器；本层不依赖 runtime。

输入是带保活对象的只读字节跨度。表保留 package/type/config 索引，值与偏移留在原字节中。构造时校验 chunk、配置、字符串池和索引边界；每个 type chunk 的 entry 在首次访问时整块校验，成功后通过 once_flag 共享结果，失败不发布成功状态。表和字符串池缓存可跨会话共享；首次校验与字符串解码同步。

ResTable 按 API19 配置选择资源，解析引用和 bag 继承，按配置缓存不可变 bag。配置及路径变更清空 bag 缓存。调用方串行化配置变更，读取已固定配置的共享表可并发。条目查询缺失返回空结果，畸形数据在首次涉及该 chunk 的查询抛 ResourceError。Overlay/idmap、动态包 ID 和新版本稀疏表不在范围内。
