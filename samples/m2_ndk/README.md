# M2 无界面 NDK 累计样本

样本只导出普通 C 入口 `ogplay_m2_entry`，不包含 Activity、JNI 或图形。入口会分配内存、
创建并等待真实 pthread，再把子线程生成的数据写入传入路径并读回校验。

```powershell
python samples/m2_ndk/tools/build.py `
  --sdk $env:ANDROID_SDK_ROOT `
  --ndk $env:ANDROID_NDK_ROOT
```

默认构建 API 19 `armeabi-v7a`，产物写入被 Git 忽略的 `out/m2-ndk/`。构建成功只证明载荷
ABI 正确；M2 出口还要求由 OGPlay 加载真实 Bionic 后执行入口并返回 0。
