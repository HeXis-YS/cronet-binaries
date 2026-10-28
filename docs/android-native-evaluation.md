# Android 原生接入评估

评估日期：2026-10-28。Chromium 基线为 156.0.8078.47；宿主依据为
[SimpleXray fe1069f6](https://github.com/HeXis-YS/SimpleXray/tree/fe1069f6dc7dc9b2ce3d4ab70835fcd14c250460)。
本次只评估 Android 简化方案，未改写 04；连接分组和轮换保持现状。
证书策略已经调整为 Chrome 内置根库与附加 SPKI pin，不读取系统信任库。

## 结论

保留 Android NDK、Bionic 和 `target_os="android"`，在现有 C ABI 适配层显式选择
需要的网络服务，再对仍然可达的 Java 依赖提供构建时选择的原生实现。
不将整个目标伪装成 Linux，也不以删除所有 Java 生成工具作为第一步。

当前库直接依赖 `//base`、`//net`、`//url`，没有使用完整的 Cronet Java engine。
简化对象是这些底层库的 Android 平台服务。上游已经提供 epoll IO 循环、原生
线程和系统解析路径，因此不需要重写它们；但少量 `IsJavaAvailable()` 分支也
不能证明全部运行路径已经适配完成。

普通 Linux arm64 产物使用 glibc sysroot，不能作为 Bionic 进程内动态库直接
替代。两者即使同为 AArch64，也有不同的用户态依赖。构建选择见上游
[sysroot.gni](https://github.com/chromium/chromium/blob/156.0.8078.47/build/config/sysroot.gni#L23)。

## SimpleXray 的实际运行边界

[TProxyService](https://github.com/HeXis-YS/SimpleXray/blob/fe1069f6dc7dc9b2ce3d4ab70835fcd14c250460/app/src/main/kotlin/org/hexis/simplexray/service/TProxyService.kt#L156)
从 `nativeLibraryDir/libxray.so` 启动独立子进程，通过 stdin 提交配置、stdout
收集日志。该核心不在 App 的 JVM 内，也没有接收 Java 网络回调的现成通道。
当前代码启动的是 Xray-core；替换为 Rust 核心后的加载和运行尚未验证。

VPN builder 使用允许/排除应用列表。默认和绕过指定应用模式会排除自身应用；
指定应用模式只接管列表中的应用。核心继承应用 UID，在自身未被纳入 VPN 的
条件下，无需为每个 socket 增加 `VpnService.protect()` IPC。
若用户把自身应用加入允许列表，这个前提不成立，不能承诺核心自动避开 VPN。
本次不修改 App 的应用选择策略。

服务中未发现 `ConnectivityManager.NetworkCallback`、`setUnderlyingNetworks`
或向核心报告网络变化的 IPC。不能把 App 负责启动/停止 VPN 等同于核心已经收到
网络切换通知。将来若需要可靠的网络变化通知，宿主可报告变化，由核心调用上游
被动 notifier；这需要 App 与核心共同增加接口。

App 当前仅打包 arm64-v8a，使用 `useLegacyPackaging=true`。新核心和
`libcronet.so` 的部署还需要明确动态库路径：现有启动函数没有设置
`XRAY_CRONET_LIBRARY` 或 `LD_LIBRARY_PATH`，工作目录为 `filesDir`，不能假定
`dlopen("libcronet.so")` 必然能找到相邻库。可在集成时传入绝对库路径。

文档与代码有一处版本不一致：SimpleXray README 要求 Android 10+，
`app/build.gradle` 的 `minSdk=26` 则允许 Android 8.0+。本库当前最低 API 为 24；
实际发布支持范围应在集成时统一，本评估不修改宿主配置。

## 可复用的上游实现和需要处理的边界

以下是针对当前入口的源码核对，不是完整运行调用图或运行通过证明。

| 部分 | 已核对的事实 | 简化方向与代价 |
| --- | --- | --- |
| IO 与线程 | 引擎选择 IO message pump；上游 Android IO 使用 `MessagePumpEpoll`。线程实现只有特定实时音频路径进入 Java。 | 保留上游 IO/普通线程，不替换整个 Android message pump 或调度器。 |
| CA 与 pin | 07 在当前构建选择 `CreateChromeOnlySystemTrustStore`；pin 在本地验证器中计算，然后委托普通验证。 | 不需要文件根库或 Java 证书验证；CA 更新随库发布。 |
| TLS 平台策略 | builder 默认创建 `EchModeGetterAndroid`，其查询进入 Android network library。 | 适配层可设置已有的 `SSLConfigServiceDefaults`，不传平台 getter，采用上游 opportunistic ECH 默认值；不再跟随 App 的 Java ECH 策略。 |
| 代理 | 当前适配层没有显式配置代理；builder 将空的 proxy config service 传给要求非空的通用工厂。 | 用现有 `ConfiguredProxyResolutionService::CreateDirect()` 为主连接和证书下载显式指定直连；不引入系统代理监听。源码表明当前路径有空配置前提问题，未运行复现。 |
| DNS | 无指定 network handle 时，`AddrInfoGetter` 调用 Bionic `getaddrinfo()`；Android DNS 配置读取已有无 Java 时返回不可用的分支。 | 保留系统解析，覆盖证书 AIA；不要复制 Linux 的 `/etc/resolv.conf` 解析。网络变化后缓存失效仍需处理。 |
| 网络变化 | 上游 Android 有 `NetworkChangeNotifierPassive`；当前引擎没有创建 notifier，宿主也未发送变化事件。 | 优先复用被动 notifier。需要通知时由宿主经核心转发，不在库里复制 App 的 Java 监听器。未接入前不能承诺完整网络切换/QUIC 迁移。 |
| 网络绑定 | 上游已有 `android_setsocknetwork` 和指定网络解析接口；当前 C ABI 不暴露 network handle。 | 保留上游实现，不新增自制替代；当前模型不要求每 socket VPN 保护。 |
| 设备与进程信息 | 04 用系统属性获取设备字段，为 APK、Activity 和 UI 信息提供默认值。 | 按实际可达依赖拆分原生实现；不能把“独立进程没有 Activity”扩大为所有平台查询都返回成功。SDK 信息仍需真实获取。 |
| 平台附加服务 | TrafficStats、Java QUIC socket 注册等目前由 04 在无 JVM 时跳过。 | 不需要的服务从原生实现中明确排除；它们与必要的 socket 收发分开处理。 |
| 构建工具 | Android GN 目标仍生成 JNI 头和 Java 依赖配置，06 补齐 params 依赖。 | 初期保留 JDK/生成器。运行时无 JVM 与构建时不用 Java 是两个目标；只有构建依赖被实际解除后才能删除工具。 |

对应上游实现：

- [IO 平台选择](https://github.com/chromium/chromium/blob/156.0.8078.47/base/message_loop/message_pump_for_io.h)
- [builder 的 TLS 与代理选择](https://github.com/chromium/chromium/blob/156.0.8078.47/net/url_request/url_request_context_builder.cc)
- [系统地址解析](https://github.com/chromium/chromium/blob/156.0.8078.47/net/dns/address_info.cc#L175)
- [Android 网络接口](https://github.com/chromium/chromium/blob/156.0.8078.47/net/android/network_library.cc)
- [被动网络通知接口](https://github.com/chromium/chromium/blob/156.0.8078.47/net/base/network_change_notifier_passive.h)

## 建议实施顺序及验收

1. 先在适配层显式配置直连代理和默认 TLS 策略，复用现有接口，减少进入 Java
   平台服务的路径。这是后续实现，本次没有修改。
2. 对剩余必需的平台符号增加原生源文件选择，替换 04 中相应修改。只引入一个
   对应本库目标的选择条件，不改变 `is_android` 的 ABI 含义；保持 H2/H3、取消、
   缓冲区和验证语义。避免将本地原生实现与 Java 默认实现同时链接。
3. 在实际 SimpleXray 子进程中验证加载、无 JVM 初始化、H2/H3 双向流、取消、
   DNS/AIA、有效与无效 CA/域名/pin，以及网络切换后的行为。完整编译放在 CI，
   不以补丁应用成功代替运行验证。网络通知 IPC 的实施取决于确定的切换要求。
4. 原生运行路径通过后，再依据实际 GN 依赖决定能否去掉 JNI/Java 生成目标、
   JDK/SDK 包和 06。不能预先承诺它们都可删除。

建议在 Chromium 下游分支中维护平台源文件选择及实现，版本固定在发布 tag，
保留 Linux/Windows 的平台行为。分支应成为唯一修改来源，不再平行手写另一套
补丁。分支迁移、04 重写、宿主修改和后续 CI 均未在本次执行。

本次证据为固定版本源码、SimpleXray 固定提交和轻量补丁应用检查；未进行本地
Chromium 构建、Android 设备运行或性能测量，因此不报告性能收益或完整兼容性。
