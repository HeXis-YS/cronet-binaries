# Chromium 原生传输补丁基线

本文固定记录 2026-10-28 确定的补丁范围、设计理由和能力边界。它不记录逐次 CI
结果或排障进度；后续构建修复不更新本文。实际补丁及应用顺序以
[tools/patches/series](tools/patches/series) 为准，操作入口见 [README](README.md)。
历史审查及被替代的方案保留在 Git 历史中。

## 目标与取舍

库面向 hray-core，通过 C ABI 提供 Chromium 原生双向流，目标平台为 Linux、
Windows 和 Android。Android 由 Rust 核心直接调用，不提供 Java/JNI 消费接口。

维护重点是原生接入、CA 校验与附加 pin、Chromium 连接池、显式连接隔离和
HTTP authority。优先减少上游修改及升级维护成本；不把缩减源码、依赖体积或
编译时间本身作为必须保留的功能。ICU、Rust、libc++ 和 protobuf 使用上游实现。

## 保留的补丁

以下 12 项组成本次固定基线。编号用于追溯原补丁，不表示连续编号或依赖优先级；
应用顺序由 series 定义。“保留”表示进入基线，不代表跨平台验收已经通过。

| 编号 | 作用 | 保留理由与边界 |
| --- | --- | --- |
| 03 | Android 网络原生适配 | 支持无 JVM 接入；指定网络绑定和部分平台查询仍为 stub，不能视为完整 Android 网络语义。 |
| 04 | Android base 原生适配 | 使用原生系统属性读取 SDK 等信息；其余 stub 的行为不能等同 Java 平台实现。 |
| 05 | jni_zero 适配 | 处理原生依赖路径中的 JNI 耦合；不对外增加 Java 接口。 |
| 06 | URL 的 Java 构建条件 | 通过 Java 模板开关限制相关目标，保留原生 URL 实现。 |
| 07 | 无 Java 的 Android 构建配置 | 禁用 Java 模板时仍提供原生测试、runner 和公共 Android 配置；保留显式测试选择和上游断言。 |
| 08 | Android libatomic 链接调整 | 在不用 Chromium sysroot 的配置下避免额外添加 libatomic；属于工具链兼容项。 |
| 18 | Unix/Android 文件信任库 | 将文件来源的根证书接入现有验证器，并保留 Chrome 根库；不承诺完整复现平台用户证书策略。 |
| 21 | 连接池上限调整 | 配合引擎设置的池和 proxy-chain 上限 2048、分组上限 2040；保留 Chromium 调度与连接池实现。 |
| 26 | 显式连接隔离字段 | 从 C ABI 传入隔离信息，构造 NetworkIsolationKey/NetworkAnonymizationKey；不使用伪装 HTTP header 传递控制信息。 |
| 27 | authority 与 TLS 名称分离 | 将 authority 作为请求字段传入 HTTP/2、HTTP/3 头转换；保留独立的 TLS 名称和证书校验。 |
| 28 | Windows SDK 兼容 | 调整 SDK 要求及相关版本定义，适配选定的原生 Windows 工具链；属于环境兼容项。 |
| 29 | sysroot 生成器调整 | 保留现有 sysroot 生成路径的包集、架构及 glibc 处理修改；属于构建环境适配，不是传输功能。 |

补丁文件位于 [tools/patches](tools/patches)。08、28、29 的适用性取决于工具链和
构建环境；本文不将它们描述为永久的平台需求。

## 未纳入基线的修改

- 01、32–39：不再通过大范围删除测试及其他 GN 目标来控制构建范围。
- 02、13、14、16、17、41：移除 Rust、ICU、标准库及 protoc 的替代实现或裁剪，
  恢复上游路径。旧 ICU dummy 的 IDN/字符集限制不作为当前接口要求。
- 09、24、25、30：移除旧的独立构建、精简启动及配套 GN 修改，使用上游构建文件。
- 10、11、12、31：不再修改全局日期宏、builtins 和 ThinLTO 默认配置；
  生产 LTO 由构建参数和本地最终链接目标表达。
- 19、40：通过 GN 参数选择 builtin verifier，去掉重复的源文件增删补丁。
- 15、20、22、23：恢复上游未知 CPU 检查、AIA 解析、H2 日志及 RST_STREAM 行为。

未列入 series 的旧补丁文件即使仍在仓库内，也不参与源码准备。

## Android 与证书边界

Android 网络适配中的默认解析仍可使用系统 getaddrinfo；指定网络解析、网络绑定
及平台 DNS 查询的 stub 不能推广为“所有 DNS 都不可用”，也不能据此承诺网络切换
或平台 DNS 行为完整。引擎主连接使用 host mapping，证书抓取 context 有独立解析
路径，因此 AIA 等证书网络请求同样属于验证范围。

在关闭 NSS 服务端证书支持的配置下，补丁 18 将 TrustStoreChrome 与文件信任库
组合。SSL_CERT_FILE 和 SSL_CERT_DIR 调整文件来源，不替换全部信任根。
额外安装的用户/企业 CA 不属于要求，但也不主动排除文件信任库中已有的证书；
该路径不保证复现平台禁用根、用户根及更新策略的全部语义。系统 CA 校验仍为
要求，pin 是附加约束，不能替代 CA 和主机名校验。

相关实现位于 [engine.cc](src/components/xray_native/engine.cc)、
[补丁 03](tools/patches/03-31bae8d4e.patch) 和
[补丁 18](tools/patches/18-64d2befbc.patch)。

## 原生接口与所有权

原生路径为 Rust 回调状态、xray_native Stream、net::BidirectionalStream。
C ABI 负责网络线程投递、缓冲区生命周期、单次读写、取消和终止；连接调度由
Chromium 承担。成功、失败和取消通过 done(error) 通知，0 表示成功。

每个流持有读写缓冲区，终止过程中须先停止底层 I/O，再通知调用方释放缓冲区。
context 的生命周期覆盖其原生流；回调和异步销毁的顺序属于接口验证范围。
接口与具体实现分别见 [xray_stream.h](src/components/xray_native/include/xray_stream.h)
和 [stream.cc](src/components/xray_native/stream.cc)。

## 构建与验证依据

[prepare-source.sh](tools/prepare-source.sh) 解包指定版本源码，依次应用 series，
再复制本地适配代码和脚本。构建使用上游 GN 文件，通过 root target 限定本地库
依赖范围；这不会跳过已加载 BUILD.gn 中其他目标的声明。

[build.sh](src/build.sh) 提供平台参数、builtin verifier/NSS 配置及 ThinLTO 开关；
[本地目标](src/components/xray_native/BUILD.gn) 为最终共享库选择链接优化配置。
工具链安装由 [get-clang.sh](src/get-clang.sh) 负责，Windows 使用原生工具链。
具体 runner、SDK、参数及检出规则以 workflow、脚本和 .gitattributes 为准。

本文描述设计基线，不是构建或互通成功证明。补丁解析、局部 GN 实验、完整配置、
编译链接及运行互通是不同层次的证据；一个平台的结果不能替代其他平台。
逐次验证条件和原始输出保存在 CI 日志、构建产物及本地运行记录中，交接状态
保存在交接笔记中，不回填本文。
