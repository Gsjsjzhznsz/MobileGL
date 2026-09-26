# mg-3backends —— MobileGL + MobileGlues 合并分支（Air 6.0 定义）

## 目标

把 MobileGL-Dev 组织下的两个 GL 实现项目合并为**一个安卓端项目**：
以 **MobileGlues 插件 App（MobileGlues-plugin）的界面**为唯一门面，把 MobileGL
核心集成进去，渲染后端三选一 —— 定义与 Air 启动器 6.0 完全一致，
为安卓设备提供 bug 恢复与 FSR 支持。

**统一项目宿主：Gsjsjzhznsz/MobileGlues-plugin，分支 `mg-3backends`。**
（本仓库保留 dispatcher 与核心构建；旧的 android-plugin / mg-air 目录是
历史方案，不再演进。）

## Air 6.0 三端定义（Task 132 悬浮浮窗，本分支的实现映射）

| Air 6.0 后端 | `MOBILEGL_BACKEND_TYPE` / config.json `backendType` | 实现 | 产物 | FSR 1.0 |
|---|---|---|---|---|
| **Vulkan 直连**（默认） | `DirectVulkan` | MobileGL 核心之 DirectVulkan 后端 | `libMobileGL.so` | 路线图（见下） |
| **GLES** | `MobileGlues` | MobileGlues 核心（GL→ES 翻译） | `libmg_gles.so` | ✅ 原生 EASU+RCAS 双管线（上游自带） |
| **OpenGL 4.0** | `DirectGLES` | MobileGL 核心之 DirectGLES 后端 | `libMobileGL.so` | 路线图（见下） |

Air 6.0 的切换器形态（用户四次否决后定型的 Task 132）：
**MobileGlues 分区里单一「渲染后端」pick 行，原地三选一，默认 Vulkan 直连；
严禁拆成三条独立渲染器条目或二级菜单页。** 插件 App 的渲染设置组首行即此行
（Material 主题单选浮窗 / Miuix 主题下拉），双主题同位。

## 架构（统一项目内）

```
启动器（FCL/Zalith/Pojav 族，读插件 manifest 的 renderer 契约）
   │  dlopen(rendererLib = libmobileglues.so)
   ▼
libmobileglues.so  ←── 统一入口（dispatcher，本仓库 dispatcher/）
   │  选后端：
   │    1) MOBILEGL_BACKEND_TYPE 环境变量（启动器侧可选项）
   │    2) 未设 → 读 MobileGlues config.json 的 "backendType"
   │       （MG_DIR_PATH env，缺省 /sdcard/MG —— 插件 UI 写入的那份配置）
   │    3) 都没有 → 默认 DirectVulkan
   │  DirectVulkan/DirectGLES → dlopen libMobileGL.so（MobileGL 核心，核心内部再按 env 选后端）
   │  MobileGlues            → dlopen libmg_gles.so （MobileGlues 核心）
   ▼
后端核心（保持上游原样构建，零符号手术）
```

- `dispatcher/dispatcher.c`：入口库本体。`eglGetProcAddress` 智能转发
  （核心优先，dlsym 兜底）；`gen_forwarders.py` 从 vendor 的 Khronos 头文件
  生成 2866 个懒绑定转发符号，对 varargs 原型硬失败，不留静默缺口。
- 插件 App 自带的 info getter / 跑分经 dispatcher 的 `eglGetProcAddress`
  智能转发解析核心私有符号（mg_multidraw_bench_run 等）—— 所以 GL 信息页
  与跑分都跟随所选后端。

## 统一项目的构建布局（MobileGlues-plugin 仓库）

| 模块 | 产物 | 说明 |
|---|---|---|
| `:MobileGlues` | `libmg_gles.so` | submodule 挂 Gsjsjzhznsz/MobileGlues@mg-3backends（core OUTPUT_NAME 已改名），自带 FSR1 |
| `:MobileGLCore` | `libMobileGL.so` + `libmobileglues.so` | submodule 挂 Gsjsjzhznsz/MobileGL@mg-3backends，直接调本仓库根 CMake（核心 + dispatcher 一次产出） |
| `:app` | 插件 APK | MobileGlues 界面 + 后端 pick 行 + FSR1 开关；jniLibs 打包上述三库 |

两个核心树的 glslang/SPIRV-Cross 各自 vendor 且选项不同，**必须保持两次
独立 CMake 调用**，Gradle 模块天然满足。

## 配置流

- 插件 UI（本 App）写 `/sdcard/MG/config.json`（或启动器传入的 MG 目录）：
  - `backendType`：三端字符串，与 env 取值一字不差（dispatcher 与 UI 共享语义）；
  - `fsr1Setting`：FSR1 档位（0=关/1=UltraQuality/2=Quality/3=Balanced/4=Performance），
    MobileGlues 核心原生读取；`MOBILEGL_FSR1` env 覆盖仍可用（env > config.json）。
- 换后端后**下次启动游戏生效**（dispatcher 在启动器进程首次 GL/EGL 调用时定桩）。

## FSR 路线图（2026-09 更新：内置 FSR1 已升级为 Arm ASR）

内核统一为 **Arm® Accuracy Super Resolution™**（Arm 对 AMD FidelityFX
FSR 1.0.2 的移动优化版，MIT，
github.com/arm/accuracy-super-resolution），三端同源：

1. ✅ GLES 端（MobileGlues 后端）：`gl/FSR1/` 重写为 EASU + RCAS 双 pass，
   常量用 vendored Arm CPU 头（`include/ffxm/`）逐位计算；textureGather 以
   texelFetch 按 GLES 规范仿真（ESSL 300 可转换，旧 AMD 单 pass 在 ES 3.0
   设备上从未成功转换过）；viewport/scissor 随重定向改写，预设有真实降分
   辨率收益；UI 档位开关已接。
2. ✅ OpenGL 4.0 端（DirectGLES）：`MG_Backend/DirectGLES/FSR1.cpp`，
   同一双 pass 架构。ESSL 着色器预转换内嵌（后端零转换器依赖）；
   `BindFramebufferId(0)` 重定向 + 呈现前 EASU/RCAS + shadow 物理值复位；
   viewport/scissor 在渲染状态同步处改写；glBlitFramebuffer 以逻辑默认帧
   缓冲为端点的矩形按同一比例改写（GLES 不裁剪越界 blit——整帧以
   GL_INVALID_OPERATION 拒绝，上采样只能拿到陈旧内容）。开关：
   `MOBILEGL_FSR1` env（0=关 / 1..4=档位，与文档化的启动器侧覆盖一致）；
   锐化：`MOBILEGL_FSR1_SHARPNESS`（0-100，缺省 90 = 0.2 stops，与 GLES 端
   同映射）。两者由 dispatcher 从 config.json 桥接（env 优先，
   overwrite=0），UI 开关不需要 env 也能到达本端；dispatcher 同时把
   config.json `backendType` 回填进 env，杜绝入口库与核心的后端选型分歧，
   并在「fsr1 已请求 + 后端 DirectVulkan」组合下打一次性 WARN（该组合
   无法生效，见第 3 条）。
3. ⏳ Vulkan 直连端（DirectVulkan）：呈现层 FSR1（低分辨率渲染 + EASU/RCAS
   compute 放大锐化后进 swapchain）。安卓无需 iOS 的 Metal 拦截方案，
   直接在 Vulkan 侧实现。复用同一组 Arm 着色器源（GLSL 450 → SPIR-V）。
   现状：默认帧缓冲直接渲染进 swapchain 图像（VkRenderPassManager 的
   SwapchainColor 目标），无现成 offscreen 中转可挂；插件设置页与
   dispatcher 日志均已明确标注该后端暂不支持 FSR1。

已知取舍（两端共用）： fabulous 式 FBO0↔用户 FBO blit 路径在 FSR 重定向
下只保证常规管线正确；上下文丢失恢复与 Vulkan 端接入见各自文档。
GLES 端 2026-09-26 补：应用发起、落在重定向上的 blit（逻辑 FBO0 为读或
写端点）矩形按 surface→render 比例改写（与 viewport/scissor 同一设计），
窗口尺寸的整帧传递不再越界被裁；InitFSRResources 不再在帧中裸绑 render
FBO（此前一次帧内 tracked/driver 分歧可产生异常帧）。

## 与上游的同步

- MobileGL 侧：本分支基于上游 dev（fff9d639，含 26.3-rc-3 修复）。
- MobileGlues 侧：submodule 指向 Gsjsjzhznsz/MobileGlues 的 mg-3backends
  分支（上游 main + 集成补丁），上游更新后在 fork 分支 rebase 即可。
- MobileGlues-plugin 侧：Gsjsjzhznsz/MobileGlues-plugin@mg-3backends
  （上游 main + 上述集成），上游 App 更新后 merge 即可。
