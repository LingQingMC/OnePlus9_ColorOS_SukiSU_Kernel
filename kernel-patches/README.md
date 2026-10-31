# 内核主树补丁

这里是对 **OnePlusOSS `android_kernel_oneplus_sm8350`** 内核主树（非 SukiSU 源码）的六处修改。

这些文件是**修改后的完整文件**，直接覆盖 OnePlusOSS 内核源码对应路径即可（`pgtable.h` 为新增文件，直接放入即可）。

## 修改清单

### 1. `kernel/module.c` — CRC 校验放宽

**路径**：`modules/kernel/msm-5.4/kernel/module.c`

**改动点**：`check_version()` 函数

**原因**：本内核由 OnePlusOSS 源码 + 原机内嵌配置复刻编译，与出货内核同源同配置。
但 genksyms 的 CRC 是对预处理输出算的哈希，我方宿主工具链与原厂 clang 11.0.2 构建环境
存在系统性差异，导致 CRC 错位（真实 ABI 兼容）。

**效果**：CRC 不匹配时仅 `pr_debug` 记录，不拒绝加载。vermagic（含 modversions 标志）
仍由 `check_modinfo` 严格校验。这样 vendor_boot 里的 59 个厂商 .ko 模块能正常加载。

### 2. `security/selinux/ss/services.c` — sepolicy 注入时机修复

**路径**：`modules/kernel/msm-5.4/security/selinux/ss/services.c`

**改动点**：`security_load_policy()` 重载成功出口处，加 `#ifdef CONFIG_KSU` 钩子，调用
`ksu_reapply_sepolicy_after_reload()`。

**原因**：`security_load_policy()` 重载时用 `memcpy` 整块替换 policydb，会抹掉 SukiSU
在 `init second_stage` 时注入的 `u:r:ksu:s0` 域。ColorOS 每次启动现场编译 policy，时序
不稳定，导致同一内核两次启动行为不一致（授权偶发失败）。

**效果**：每次 policy 重载后重新注入，保证注入永远发生在最后一次 load 之后。

### 3. `fs/namespace.c` — 导出 `path_umount()`

**路径**：`modules/kernel/msm-5.4/fs/namespace.c`

**改动点**：文件末尾新增 `path_umount()` 的薄封装并 `EXPORT_SYMBOL`。

**原因**：SukiSU 的模块 umount 功能依赖 `path_umount()`，该函数 5.9+ 才存在。5.4 上
`do_umount` 是文件内静态函数，故导出薄封装供驱动调用。

**效果**：SukiSU 的 "Unmount modules" 功能在 5.4 上可用。

### 4. `include/linux/pgtable.h` — 兼容头（新增文件）

**路径**：`modules/kernel/msm-5.4/include/linux/pgtable.h`

**改动点**：新增兼容头，`#include <asm/pgtable.h>` + `#include <asm/pgtable-types.h>`。

**原因**：`linux/pgtable.h` 自 Linux 5.8 起才独立存在。SukiSU 源码 include 了它，5.4 上
会 fatal error，故提供此兼容头。

**效果**：SukiSU 源码在 5.4 上能正常编译。

### 5. `drivers/Makefile` — SukiSU 编译入口

**路径**：`modules/kernel/msm-5.4/drivers/Makefile`

**改动点**：加入 `obj-$(CONFIG_KSU) += kernelsu/`。

**原因**：让内核在 `CONFIG_KSU=y` 时编译 `drivers/kernelsu/`（即 SukiSU 内核代码）。

### 6. `drivers/Kconfig` — SukiSU 配置入口

**路径**：`modules/kernel/msm-5.4/drivers/Kconfig`

**改动点**：加入 `source "drivers/kernelsu/Kconfig"`。

**原因**：让 `make menuconfig` 能识别 SukiSU 的 `CONFIG_KSU` 等配置项。

## 应用方式

```bash
cd modules/kernel/msm-5.4
cp kernel-patches/kernel/module.c                kernel/module.c
cp kernel-patches/security/selinux/ss/services.c security/selinux/ss/services.c
cp kernel-patches/fs/namespace.c                 fs/namespace.c
cp kernel-patches/include/linux/pgtable.h        include/linux/pgtable.h
cp kernel-patches/drivers/Makefile               drivers/Makefile
cp kernel-patches/drivers/Kconfig                drivers/Kconfig
```
