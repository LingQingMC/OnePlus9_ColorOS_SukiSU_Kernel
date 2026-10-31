# OnePlus 9 (lemonade) SukiSU Kernel

为**一加 9（lemonade / LE2110）** 定制的、集成 **SukiSU-Ultra v4.3.0** 的内核集成方案，目标系统 **ColorOS 14.0.0.1901(CN01)**，内核 **Linux 5.4.254**。

> 这是首个针对一加 9（sm8350）5.4 内核的 SukiSU 完整集成，全网首发。root 授权、管理器、模块功能均已实测打通。

---

## 这是什么

SukiSU-Ultra 官方上游面向 GKI 2.0（内核 5.10+）设备。一加 9 是非 GKI 的 5.4 内核，无法直接使用官方集成脚本。

本仓库解决了 **SukiSU 在 5.4 内核上的一系列兼容问题**，并额外适配了一加 9 ColorOS 的私有行为，使 SukiSU 能在 5.4 上完整工作。

## 目录结构

```
├── KernelSU/            SukiSU-Ultra 完整源码（含 5.4 兼容补丁）
│   ├── kernel/          内核侧实现（已打 5.4 补丁）
│   ├── manager/         管理器 App 源码
│   └── userspace/       ksud / su 用户态
├── kernel-patches/      对 OnePlusOSS 内核主树的补丁（覆盖式，共 6 处）
│   ├── drivers/         Makefile / Kconfig 的 SukiSU 编译入口
│   ├── kernel/module.c  CRC 校验放宽（加载厂商 .ko 模块）
│   ├── fs/namespace.c   导出 path_umount()
│   ├── include/linux/pgtable.h  兼容头（新增）
│   └── security/selinux/ss/services.c  sepolicy 注入时机修复
├── config/              编译配置
│   ├── stock.config     原机内嵌配置（baseline）
│   └── lemonade_sukisu.config  最终编译配置（stock + CONFIG_KSU=y）
└── README.md
```

## 相对上游 SukiSU 的 5.4 兼容改动

SukiSU-Ultra main 分支面向 5.10+，5.4 上缺以下能力，本仓库已逐一补齐：

| 问题 | 位置 | 说明 |
|------|------|------|
| 5.4 无 `struct selinux_policy` | `kernel/selinux/rules_54.c`（新增） | 原地修改 `selinux_state.ss->policydb` 注入 sepolicy，实现 root 授权链 |
| sepolicy 注入被 policy reload 覆盖 | `kernel/selinux/rules_54.c` + `security/selinux/ss/services.c` | 在 `security_load_policy` 后重新注入，解决非确定性授权失败 |
| 5.4 无 `copy_{from,to}_user_nofault` | `kernel/include/compat_54.h` | 用 `probe_user_read/write` 实现 shim |
| 5.4 无 `pmd_leaf`/`pud_leaf` 块映射判断 | `kernel/hook/arm64/patch_memory.c` | 改用 `pmd_sect`/`pud_sect`，修 KASLR 落点导致的时好时坏 |
| 5.4 seccomp 无 `action_cache` | `kernel/hook/setuid_hook.c` | 清 `TIF_SECCOMP` + 关 mode，放行管理器/allowlist 应用 |
| 5.4 `filename_trans` 结构旧 | `kernel/selinux/sepolicy.c` / `sepolicy.h` | 条件编译隔离 5.5+ 专属代码 |
| 管理器 crown 读 packages.list 失败 | `kernel/manager/throne_tracker.c` | 改用调用者 creds |
| 厂商 .ko 模块 CRC 校验失败 | `kernel/module.c`（kernel-patches） | 放宽 CRC，保留 vermagic 严格校验 |

## 如何集成

### 前置

1. 内核源码：OnePlusOSS `android_kernel_oneplus_sm8350`，分支 `oneplus/sm8350_u_14.0.0_oneplus9`
2. 闭源驱动/设备树：OnePlusOSS `android_kernel_modules_and_devicetree_oneplus_sm8350`，同名分支
3. 工具链：AOSP clang 11.0.x（r383902）+ LLD 11.0.x（对齐原机，保证厂商模块 ABI 兼容）

### 集成步骤

1. 按 OPPO `buildkernel.sh` 要求，把内核源码合并进 `modules/kernel/msm-5.4/`（编译根目录必须是 `modules/`）。

2. 把本仓库的 `KernelSU/` 放进 `modules/kernel/msm-5.4/`，并建立软链：
   ```bash
   cd modules/kernel/msm-5.4
   ln -s KernelSU/kernel drivers/kernelsu
   ```

3. 在 `drivers/Makefile` 和 `drivers/Kconfig` 里加入 kernelsu 条目（参考 SukiSU 官方集成文档）。

4. 应用内核主树补丁：
   ```bash
   cp kernel-patches/kernel/module.c           kernel/module.c
   cp kernel-patches/security/selinux/ss/services.c  security/selinux/ss/services.c
   ```

5. 用 `config/lemonade_sukisu.config` 作为编译配置（含 `CONFIG_KSU=y`）。

### 编译

```bash
cd modules/kernel/msm-5.4
make O=out2 ARCH=arm64 \
  CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm \
  OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip \
  CLANG_TRIPLE=aarch64-linux-gnu- \
  CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- \
  LLVM_IAS=1 \
  KCFLAGS="-D__ANDROID_COMMON_KERNEL__" \
  Image
```

## 打包与刷入

用 `mkbootimg` 打包（header v3，ramdisk 用原机）：
```bash
mkbootimg --header_version 3 --kernel Image --ramdisk <原机ramdisk> \
  --os_version 12.0.0 --os_patch_level 2025-04 --output boot.img
```

**⚠️ 重要：一加 9 的 boot 分区受 AVB 校验，只 patch boot 就刷会导致无法开机（只进 fastboot）。必须先禁用 vbmeta 校验：**

```bash
fastboot --disable-verity --disable-verification flash vbmeta_a vbmeta_a.img
fastboot --disable-verity --disable-verification flash vbmeta_system_a vbmeta_system_a.img
fastboot --disable-verity --disable-verification flash vbmeta_vendor_a vbmeta_vendor_a.img
fastboot flash boot_a boot.img
fastboot reboot
```

## 验证结果

- root 授权链：`su -c id` 返回 `uid=0(root) context=u:r:ksu:s0`
- 管理器：`com.sukisu.ultra` 正常 crown，显示「工作中」
- 模块：安装、post-fs-data.sh / service.sh 执行、文件落盘全部实测通过

## 许可

- `KernelSU/kernel/` 及内核补丁遵循 GPL-2.0-only
- 其余遵循 SukiSU-Ultra 上游许可（见 `KernelSU/LICENSE`）

## 致谢

- [SukiSU-Ultra](https://github.com/SukiSU-Ultra/SukiSU-Ultra) — 上游
- [KernelSU](https://github.com/tiann/KernelSU) — 上游的原始项目
