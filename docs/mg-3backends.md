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

## FSR 路线图

1. ✅ GLES 端：MobileGlues 上游 FSR1（EASU+RCAS），UI 档位开关已接。
2. OpenGL 4.0 端（DirectGLES）：把 FSR1 双 pass 接入 DirectGLES 呈现路径
   （fb0 重定向 + swap 前放大锐化，复用 MobileGlues 的 FSR 着色器源）。
3. Vulkan 直连端（DirectVulkan）：呈现层 FSR1（低分辨率渲染 + EASU/RCAS
   compute 放大锐化后进 swapchain）。安卓无需 iOS 的 Metal 拦截方案，
   直接在 Vulkan 侧实现。

## 与上游的同步

- MobileGL 侧：本分支基于上游 dev（fff9d639，含 26.3-rc-3 修复）。
- MobileGlues 侧：submodule 指向 Gsjsjzhznsz/MobileGlues 的 mg-3backends
  分支（上游 main + 集成补丁），上游更新后在 fork 分支 rebase 即可。
- MobileGlues-plugin 侧：Gsjsjzhznsz/MobileGlues-plugin@mg-3backends
  （上游 main + 上述集成），上游 App 更新后 merge 即可。
