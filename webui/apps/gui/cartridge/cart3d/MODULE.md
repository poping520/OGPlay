# 模块：cart3d 包

独立 IIFE `cart3d.js`（vite.cart3d.config.ts），与 gui.js 同目录，只经 `api.ts` 的 `Cart3D` 接口与启动器交互。
内含 three、纯 JS Draco 解码器（draco.ts）与内联 `cartridge.glb`/AO（模型由 `tools/build_cartridge_model.py` 生成）；
scene.ts 加载模型并按条目印刷贴纸/背面记录/书脊、构建材质，inspect.ts 提供卡带展示的交互视图。
主线程解码，不放宽 CSP（无 fetch、worker、wasm）。印刷只用宿主事实；读不到启动器语言，解码器错误保持原文。
