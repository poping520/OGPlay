# 模块：guest WebView provider

API 19 ROM 的 `WebViewFactory` 通过固定类名加载 OGPlay 的 Java provider。工厂入口转发到
`org.ogplay.webview.WebServices`，由它创建每个 View 的 provider 并持有进程内 WebKit 服务。
`android.webkit` 公共 API 和线程检查由原版 ROM 执行。BootDex 的编译期声明来自 API 19 ROM
接口，构建时剔除。

`Provider` 经 `WebView.PrivateAccess` 接入 ROM View 协议，持有设置、客户端、JS 接口登记、
缩放和查找状态。所有引擎操作通过 `WebEngine`，事件在 View 所属 Looper 交付。
`NoWebEngine` 维护页面生命周期和历史，网络导航产生 DNS 失败，本地导航检查 guest 内容并在
成功时记录显示降级。绘制仅填充背景色。JS 请求记账，页面和脚本均不执行。销毁取消待交付
的加载和查找，释放客户端及 JS 接口引用。Cookie、存储配额、地理权限和认证数据仅存在于 guest
进程内。模块不引入 native ABI 或宿主浏览器、网络和存储服务。
