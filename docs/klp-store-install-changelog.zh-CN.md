**KLP：store 主动安装 L1D key 的更改记录**

记录日期：2026-09-17。

实现基于 `klpdev_wholeCache_baseaddr_valcmp_oracle` 分支的 `175a24148b`，该提交的父提交是 `36302a4644774`。本次移植 `fix2.patch` 中的 store 安装功能，并适配当前分支已有的 split load、未知 base、禁用模式等修复。记录对应当前工作区改动，尚未暂存或提交。

**1. 功能目的与行为变化**

此前，O3 store 已有地址 key 的生成逻辑，但 LSQ 创建的普通写 Packet 没有将这些 KLP 元数据带入缓存。L1D 的权限安装主要由 unconditional KLP load 完成。因此，store 初始化数据后，后续 load 仍可能因为相应 granule 尚未建立权限而校验失败并 replay。

本次增加默认关闭的 `klp_store_install` 开关。开启后，受 KLP 标记的普通 O3 store 将地址 key 传入 L1D，并在实际写入成功后，将该 key 安装到写请求覆盖的全部 granule。

这里安装的是 **store 访问地址的 key**，不是写入数据的 provenance。store 不需要先通过旧 key 的校验；若 granule 已经有 key，新 store 可以覆盖它。后续 load 使用现有机制比较自身 key 与缓存中保存的 key。

本次没有修改 candidate 的产生、传播、大小比较或 hashing 函数，也没有移植 patch 中的 `klp_prov_propagate`、通知接口和审计模块。

**2. 端到端执行路径**

```text
现有 ISA/O3 地址 key 生成
  → DynInst 保存 key 和 baseUnknown 状态
  → 普通 store 提交，store queue 允许写回
  → LSQ 构造带有 KLP 元数据的写 Packet
  → L1D 命中，或 miss 填充后满足原始 store 请求
  → checkWrite() 确认允许写入
  → 更新缓存数据
  → 安装各个被写 granule 的 key 和有效位
  → 后续 load 使用现有 KLP 校验与 replay 流程
```

普通 store 的非推测性由现有 O3 提交和 store queue 写回顺序保证。新功能没有在执行地址生成时提前修改缓存权限。

**3. 解决 `setSecTagInCache()` 的断言冲突**

修改文件：`src/mem/cache/tags/base.cc`，函数 `BaseTags::setSecTagInCache()`。

原断言只允许 unconditional KLP load：

```cpp
assert(pkt->isKlpRead && pkt->isUnCondiReExe());
```

新增 store 调用该接口时，`isKlpRead` 为 false，因此原断言会失败。现在改为允许两种调用：

```cpp
assert((pkt->isKlpRead && pkt->isUnCondiReExe()) ||
       (pkt->isKlpWrite && pkt->isWrite()));
```

读请求仍需满足原有 unconditional KLP load 条件。写请求必须带有 `isKlpWrite` 标记，而且 Packet 的命令必须是写操作。

该修改没有给 store 设置 `isUnCondiReExe()`，因为这个字段表示现有 KLP load 的 unconditional replay 状态，不应代替普通 store 的提交状态。

以下检查继续保留：

```cpp
assert(!pkt->isBaseUnknown());
assert(val & (uint64_t{1} << 63));
assert(blk && blk->isValid());
assert(startIdx + granuleNumOfReq <= (blkSize / tag_granularity));
```

其中，低位 hash 为零仍然可以是合法 key，只要最高位有效标记为 1。未知 provenance 使用的零值不满足这个要求，且会在进入安装接口之前被过滤。

新断言本身不验证“store 已提交”；这是调用路径的约束。普通 O3 store 提交后才写回，而缓存中的安装调用位于 `blk->checkWrite(pkt)` 成功分支内。断言、调用条件和流水线顺序共同保证接口使用正确。

**4. Packet 元数据的创建与复制**

修改文件：`src/mem/packet.hh`。

新增 `isKlpWrite`，默认值为 false。普通 Packet、新建的 cache writeback 和新建的 cache-line 获取请求不会因为本次改动自动成为 KLP store。

新增 `Packet::createKlpWrite()`，复用现有带 KLP 元数据的构造函数，携带地址 key、baseUnknown 和现有 unconditional 状态，并设置 `isKlpWrite=true`。

在 `Packet(const PacketPtr, bool, bool)` 复制路径中，对 KLP store 同步复制以下字段：

- `isKlpWrite`；
- `secTagInPkt`；
- `baseUnknown`；
- `unCondiStatePkt`。

这样可避免复制后的 Packet 留下标记却丢失 key，或丢失整个 store 安装资格。本次没有重写现有 read Packet 的复制规则，也没有增加 Request 级字段。

**5. LSQ 单请求、拆分请求与 retry**

修改文件：`src/cpu/o3/lsq.hh`、`src/cpu/o3/lsq.cc`。

新增 `LSQRequest::createWritePacket()`，由 `SingleDataRequest::buildPackets()` 和 `SplitDataRequest::buildPackets()` 共用。

| 条件 | Packet 构造行为 |
| --- | --- |
| 指令没有有效的 KLP store 标记 | 普通写 Packet |
| 主请求标记为 uncacheable 或 strictly ordered | 普通写 Packet |
| baseUnknown 为 INIT，即没有生成可用的 KLP 状态 | 普通写 Packet |
| baseUnknown 为 TRUE | KLP 写 Packet，key 强制为 0，缓存不安装 |
| baseUnknown 为 FALSE | KLP 写 Packet，携带 DynInst 中的有效 key |

拆分访问使用合并后的 `mainReq()` 属性判定是否绕过 KLP 元数据构造，避免只给部分 uncacheable/ordered 访问片段附加安装语义。

跨 cache line 的 store 每个片段均使用同一条指令的 key，各自更新对应 line 中被覆盖的 granule。retry 继续复用已经构造的 Packet，不会因为端口阻塞重新生成 key。

**6. L1D 安装条件、粒度与生命周期**

修改文件：`src/mem/cache/base.cc`、`src/mem/cache/base.hh`。

安装位于 `BaseCache::satisfyRequest()` 的写分支中，并且只有 `blk->checkWrite(pkt)` 成功、缓存数据实际更新后才执行。还必须同时满足：

- KLP 未通过 `threat_model=disable` 禁用；
- `klp_store_install` 已开启；
- 当前 cache 的级别是 L1D；
- Packet 带有 `isKlpWrite` 标记；
- 请求不是 uncacheable 或 strictly ordered；
- base 已知；
- 目标不是 `tempBlock` 临时填充块。

成功安装复用 `setSecTagInCache()`：从写请求的起始偏移计算首个 granule，并根据“granule 内偏移 + 写入字节数”向上取整计算覆盖数量。即使某个 granule 只写入部分字节，该 granule 的整个 key 也会更新。

例如，16B granule 中，offset 28 的 8B store 会覆盖两个 granule；64B cache line 中，offset 60 的 8B store 会拆分到两个 line，两个片段分别安装权限。

命中和 miss 填充后的完成路径都通过 `satisfyRequest()`。MSHR 中保存的原始目标 store Packet 保留安装所需的元数据。新建的下游取 line 请求本身不负责安装 L1D store key。

未知 base 的 store 仍正常更新数据，但不安装新 key，也不增加“主动清除旧 key”的逻辑。缓存权限的失效、替换和回收继续使用已有机制；本次没有增加独立权限表或新的 squash 状态。

将安装放到成功写入分支还避免了失败写入修改权限。不过，本次没有为 RISC-V LR/SC 或 AMO 新增 KLP key 生成，不能据此将它们描述为已支持的 KLP store 指令。

**7. 参数、统计和调试信息**

`src/mem/cache/Cache.py` 新增：

```python
klp_store_install = Param.Bool(
    False, "KLP stores install their key in the L1D granules they write"
)
```

`BaseCache` 新增成员 `klpStoreInstall`，从上述参数初始化。

`KLPL1DCache` 和 `KLPPL1PL2SL3CacheHierarchy` 的构造接口增加同名参数，默认 false；KLP cache hierarchy 只把该选项传给各个 L1D。

新增统计 `klpStoreInstallNum`，计数单位为执行的安装操作，具体为：

| 情形 | 计数 |
| --- | --- |
| 单个写 Packet 安装一个或多个 granule | 加 1 |
| 跨两个 cache line 的 store，两个片段均安装成功 | 加 2 |
| 再次安装与旧 key 相同的 key | 仍计数 |
| 未知 base、开关关闭、KLP disable 或其他绕过条件 | 不计数 |

它不是独立 granule 数，也不是 key 实际发生变化的次数。

新增 `KLPDEBUG` 日志，记录安装时的物理地址、Packet 大小、key 和指令序号。日志中的地址是 Packet 的物理地址，可能与 LSQ 日志中的虚拟地址不同。

**8. 实际运行脚本与使用方法**

仓库外的脚本也已修改：

```text
/home/zhongfa/workdir/run/scripts/gem5config/riscv-se-klp.py
```

新增参数 `--klp_store_install on/off`，默认 off。开启时才向 cache hierarchy 传入 `klp_store_install=True`；默认关闭时省略这个新参数，保持脚本与旧二进制内嵌 Python 接口的兼容性。

在原有运行命令中，将下面的选项放在 workload 二进制路径之前：

```sh
--klp_store_install on
```

直接编写 Python cache 配置时，也可以设置：

```python
system.dcache.klp_store_install = True
```

本次已重新编译并验证 `build/RISCV/gem5.opt`。`gem5.fast` 和 `run/gem5bin` 中已有的副本没有因此自动更新，开启新参数时需要使用包含本次更改的二进制。

运行脚本位于另一个仓库路径，不会包含在 `klp_gem5` 的提交或 patch 中；同步本功能及其命令行入口时，需要同时保存该脚本改动。

**9. 修改文件清单**

以下路径相对 `klp_gem5`：

| 文件 | 更改内容 |
| --- | --- |
| `src/mem/packet.hh` | KLP 写标记、工厂函数及 store 元数据复制 |
| `src/cpu/o3/lsq.hh` | 声明共享写 Packet 构造函数 |
| `src/cpu/o3/lsq.cc` | 普通／拆分 store 统一传递地址 key |
| `src/mem/cache/Cache.py` | 默认关闭的安装开关 |
| `src/mem/cache/base.hh` | 缓存开关成员及安装统计 |
| `src/mem/cache/base.cc` | 成功写入后的 L1D 安装、计数及日志 |
| `src/mem/cache/tags/base.cc` | 扩展安装接口断言，保留凭据与范围检查 |
| `src/python/gem5/components/cachehierarchies/classic/caches/l1dcache.py` | L1D 构造参数 |
| `src/python/gem5/components/cachehierarchies/classic/private_l1_private_l2_shared_l3_cache_hierarchy.py` | 将开关传到 L1D |
| `tests/gem5/klp_store_install/stores.S` | 16 个 load 检查点的汇编测试 |
| `tests/gem5/klp_store_install/config.py` | O3、cache 及测试参数配置 |
| `tests/gem5/klp_store_install/run.py` | 编译、运行、解析校验结果和统计 |
| `tests/gem5/klp_store_install/README.md` | 测试运行方法和范围 |

此外新增本中文更改记录。原始 `fix2.patch` 未修改。

**10. 已完成验证**

通过带断言的 RISC-V `gem5.opt` 构建、Python 语法检查和 `git diff --check`。store 安装调用确实在该构建中执行，未触发原先的 read-only 断言冲突。

端到端回归使用 4 组配置，每组执行 16 个 load 检查点，并检查程序读到的数据及退出结果：

| 配置 | 安装次数 | KLP 首次校验结果 |
| --- | ---: | --- |
| 安装 off，spectre，2 个 L1D MSHR | 0 | 1 次通过、15 次失败；通过的是此前 load replay 已安装的零 key |
| 安装 on，spectre，2 个 L1D MSHR | 14 | 11 次通过、5 次按预期失败 |
| 安装 on，spectre，1 个 L1D MSHR | 14 | 与上一组一致，并观察到 L1D 阻塞和发送 retry |
| 安装 on，threat_model=disable | 0 | 不产生 KLP load 校验，校验总数为 0，数据检查通过 |

覆盖情形包括首次 store miss、store hit、跨 granule 写、跨 line 写、未知 provenance 的普通／拆分写、不同 key 覆盖同一地址、浮点 store、合法零哈希、被 squash 的 store，以及 MSHR 容量不足时的阻塞／重试。

开启功能时，5 个按预期失败的 load 分别检查：未知 base store、未知 base 拆分 store、被覆盖的旧 key、未知 base store 后的合法零哈希访问，以及被 squash 的 store。失败后仍沿用现有 load replay 机制完成正确的数据读取。

额外验证：

- 原有 8 个 float／split load 测试的数据检查通过，校验响应与修改前一致。
- 使用实际 `riscv-se-klp.py` 开启功能后，`config.ini` 中仅 L1D 的安装参数为 true；L1D 记录 14 次安装，其他 cache 的安装次数均为 0。
- 使用旧 `gem5.fast` 和修改后的脚本，不传新开关时，默认命令仍可正常运行。

测试结果保存在本机：

```text
/tmp/klp-store-install-4plgggeu/validation.json
/tmp/klp-store-install-4plgggeu/regression/summary.json
/tmp/klp-store-install-4plgggeu/build-opt.log
```

复现主要回归的命令：

```sh
python3 tests/gem5/klp_store_install/run.py \
  --gem5 build/RISCV/gem5.opt \
  --gcc /home/zhongfa/.local/riscv/bin/riscv64-unknown-linux-gnu-gcc \
  --outdir /tmp/klp-store-test
```

本次尚未运行完整 SPEC CPU 2017 性能实验。不可缓存／严格排序过滤、`tempBlock` 排除和 Packet 显式复制路径已经核对代码，但未分别建立独立的端到端测试；也没有新增 full-system 设备映射、Ruby 或 HTM 测试。

**11. 简洁 commit message**

下面的 message 适用于 `klp_gem5` 中的实现和测试；仓库外运行脚本需要单独保存。

```text
Add optional KLP store key installation in L1D

- Carry store credentials through LSQ packets and packet copies.
- Install keys after successful writes and allow stores in tag assertions.
- Add default-off cache configuration, statistics, and regressions.
```
