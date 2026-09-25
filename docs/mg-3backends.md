# mg-3backends —— MobileGL + MobileGlues 合并分支（Air 6.0 定义）

## 目标

把 MobileGL-Dev 组织下的两个 GL 实现项目合并为安卓端**单一渲染器入口**，
后端定义与 Air 启动器 6.0 完全一致（mg 条目三选一），
为安卓设备提供 bug 恢复与 FSR 支持。

## Air 6.0 三端定义（本分支的实现映射）

| Air 6.0 后端 | 环境变量 `MOBILEGL_BACKEND_TYPE` | 实现 | 产物 | FSR 1.0 |
|---|---|---|---|---|
| **Vulkan 直连**（默认） | `DirectVulkan` | MobileGL 核心之 DirectVulkan 后端 | `libMobileGL.so` | 路线图中（见下） |
| **GLES** | `MobileGlues` | MobileGlues 核心（GL→ES 翻译） | `libmg_gles.so` | ✅ 原生 EASU+RCAS 双管线（上游自带） |
| **OpenGL 4.0** | `DirectGLES` | MobileGL 核心之 DirectGLES 后端 | `libMobileGL.so` | 路线图中（见下） |

默认后端 = Air 6.0 的"Vulkan 直连"。

## 架构

```
启动器（Zalith/FCL/Pojav/AstroBox 兼容插件契约）
   │  dlopen(rendererGLPath = libmobileglues.so)
   ▼
libmobileglues.so  ←── 统一入口（dispatcher，本分支新增）
   │  读 MOBILEGL_BACKEND_TYPE：
   │    DirectVulkan/DirectGLES → dlopen libMobileGL.so（MobileGL 核心，核心内部再按 env 选后端）
   │    MobileGlues            → dlopen libmg_gles.so （MobileGlues 核心）
   ▼
后端核心（保持上游原样构建，零符号手术）
```

- `dispatcher/dispatcher.c`：入口库本体。构造函数期 dlopen 所选核心；
  `eglGetProcAddress` 智能转发（核心优先，dlsym 兜底）。
- `dispatcher/gen_forwarders.py`：从本仓库 vendor 的 Khronos 头文件
  （glcorearb/glext/egl/eglext）生成 2866 个懒绑定转发符号 —— 启动器经
  dlsym 拿到的所有 GL/EGL 入口都会落到这里；生成器对 varargs 原型硬失败，
  不允许 dlsym 面出现静默缺口。
- 每次调用仅一次间接跳转；核心内部互调不受影响（各自 -Bsymbolic）。

## 插件层（android-plugin）

- Zalith RendererPlugin-v2 DSL：`selectable` 环境变量键
  `MOBILEGL_BACKEND_TYPE`（DirectVulkan / MobileGlues / DirectGLES），
  启动器设置页自动出现"渲染后端"三选一 —— 对齐 Air 6.0
  「设置 > MobileGlues > 渲染后端」。
- `customizable` 键 `MOBILEGL_FSR1`：FSR1 画质档位
  （0=关 / 1=UltraQuality / 2=Quality / 3=Balanced / 4=Performance），
  经 MobileGlues 核心的 env 覆盖写入 `fsr1_setting`（env > config.json）。
- legacy manifest（boat/pojav）：`rendererLib/eglLib` 指向
  `libmobileglues.so`，pojavEnv 带 `POJAVEXEC_EGL/LIBGL_EGL`。

## 构建布局

| 模块 | 产物 | 说明 |
|---|---|---|
| `:MobileGL`（仓库根） | `libMobileGL.so` + `libmobileglues.so` | 核心 + dispatcher 同一 CMake 调用 |
| `:mg-air`（`mg-air/`） | `libmg_gles.so` | 独立 CMake 调用构建 submodule 内的 `MobileGlues-cpp/` 子目录（submodule 挂载 MobileGlues 仓库根 → Gsjsjzhznsz/MobileGlues 分支 mg-3backends），与 MobileGL 的 glslang/SPIRV-Cross 副本隔离，避免 target 冲突 |

## FSR 路线图

1. ✅ GLES 端：MobileGlues 上游 FSR1（EASU+RCAS），本分支接入 env 档位开关。
2. OpenGL 4.0 端（DirectGLES）：把 FSR1 双 pass 接入 DirectGLES 呈现路径
   （fb0 重定向 + swap 前放大锐化，复用 MobileGlues 的 FSR 着色器源）。
3. Vulkan 直连端（DirectVulkan）：呈现层 FSR1（低分辨率渲染 + EASU/RCAS
   compute 放大锐化后进 swapchain）。安卓无需 iOS 的 Metal 拦截方案，
   直接在 Vulkan 侧实现。

## 与上游的同步

- MobileGL 侧：本分支基于上游 dev（fff9d639，含 26.3-rc-3 修复）。
- MobileGlues 侧：submodule 指向 Gsjsjzhznsz/MobileGlues 的 mg-3backends
  分支（上游 main + 3 个集成补丁），上游更新后在 fork 分支 rebase 即可。
