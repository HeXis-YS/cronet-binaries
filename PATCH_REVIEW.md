# Chromium native transport patch review

审查日期：2026-10-27。对象：`tools/patches/series` 的 41 个补丁及本地
`components/grpc_support` 适配层。本文是源码审查，不是删除补丁或改变架构的授权记录。
Linux 库仍在首次完整构建；Windows、Android 尚无构建或运行结论。

边界：最终消费者是 hray-core，Android 也由 Rust 核心调用 C ABI，不提供 Java/JNI
消费接口。Chromium 内部对 JNI 的依赖仍需处理。Windows、Linux、Android、CA+pin、
原生连接池及固定隔离分组必须保留。生产 LTO 有价值，12、31 按用户决定暂时保留。

## 逐项结论

“条件保留”表示当前实现依赖它，不表示上游没有更简单的替代；“建议删除”尚未执行。

| 编号 | 内容 | 审查结论与依据 |
|---|---|---|
| 01 | 删除测试和无关构建目标 | 保留裁剪目标，重整实现。跨组件大补丁与32–39重叠；删除不可达测试目标不等于减少相同数量的实际编译任务。 |
| 02 | 移除 Chromium Rust，替换 JSON 等实现 | 待比较。减少跨平台工具链，但维护替代解析器和 structured headers 路径；hray-core 本就使用 Rust，不能以“不用 Rust”为必要性依据。 |
| 03 | Android 网络 stub | 条件保留纯原生接入能力，收缩 stub。DNS配置查询返回false、网络绑定返回ERR_NOT_IMPLEMENTED、指定网络解析返回EAI_SYSTEM；不能声称平台语义完整。 |
| 04 | Android base stub | 条件保留，逐函数核对。系统信息、线程、应用状态等行为并非都因“不提供JNI入口”而可以虚构。 |
| 05 | jni_zero 去 JNI | 当前无JVM实现条件保留；应仅覆盖可达JNI依赖。 |
| 06 | URL Android/Java裁剪 | 有用，但与32重叠；整合后不保留重复修改。 |
| 07 | 禁用Java模板 | 符合纯原生Android交付；保留。 |
| 08 | Android不额外链接libatomic | 工具链条件项，待当前NDK/Clang链接确认；不能仅沿用旧故障。 |
| 09 | 独立构建与NDK版本配置 | 独立构建有用；NDK固定值应另行核对，避免把构建模式和工具链版本绑在一起。 |
| 10 | 官方构建清除日期宏 | 非必要运行功能；可复现构建有价值但尚非明确验收，建议恢复上游或采用现成配置。 |
| 11 | 禁止显式链接Clang builtins | 条件项；与02耦合，需三平台链接证据再判断。 |
| 12 | ThinLTO最大优化 | 按用户要求暂时保留；当前build.sh关闭LTO，生产配置尚待落实，不能宣称生产优化已启用。 |
| 13 | ICU移除与dummy替代 | 有功能损失。IDNToASCII直接false，字符集转换也直接false；Unicode域名支持未经授权不应被静默缩减。需比较上游替代或保留ICU。 |
| 14 | 移除ICU的32位时间处理 | 与13耦合。32位Android明确使用time64_t及mktime64/gmtime64_r，不存在此前推测的32位SysTime问题；风险限于实际使用窄SysTime的平台。不能据此要求Android恢复ICU，也不能据此证明整个ICU移除安全。 |
| 15 | 删除未知CPU的#error | 对当前x86/x64/arm/arm64无用途；建议删除。 |
| 16 | libc++/abi无异常与RTTI裁剪 | 可选优化，不是协议要求；应与17成组比较收益与维护成本。 |
| 17 | Windows无异常标准库修复 | 仅16保留时条件需要；尚无Windows编译证据。 |
| 18 | 自行读取Unix/Android CA文件 | CA来源能力有用，实现未定。加载静态文件不等于Android平台信任语义，需比较Chromium信任库与纯原生限制。 |
| 19 | builtin verifier选择 | CA验证实现方向可用，但源文件列表改动引出40；应收缩到需要的平台选择。 |
| 20 | PKCS#7 AIA扩展 | 非当前需求；建议恢复上游行为。移除意味着少一种补链格式，不是禁用CA验证。 |
| 21 | 放宽连接池上限检查 | 当前引擎设置2048/2040直接依赖它；需核对代理链上限是否与直连XHTTP路径相关，固定分组并不自动证明所有上限修改必要。 |
| 22 | H2无效流日志降级 | 无运行能力增益，建议删除。 |
| 23 | RST前插入填充DATA | 非最小XHTTP能力，建议移出基线。它改变线上帧和流控；原说明承认额外WINDOW_UPDATE可能产生特征，未提供本项目收益证据。 |
| 24 | Brotli sanitizer import | 当前精简GN上下文需要；保留或并入构建裁剪。 |
| 25 | 独立Perfetto目标调整 | 当前构建条件保留；需与01和33–39一起整理。 |
| 26 | 双向流隔离key | 必要能力，私有header传参非必要。比较直接请求字段；当前修改net/http/bidirectional_stream.cc。 |
| 27 | HTTP authority与TLS名称分离 | 必要能力，当前全局spdy转换补丁非唯一实现。需比较局部请求接入点。 |
| 28 | 降低Windows SDK要求 | CI环境条件项；优先使用满足上游要求的SDK，仅在实际无法满足时维护。 |
| 29 | sysroot生成器裁剪 | 精简依赖有用，其他架构、glibc重写及打包行为变化应拆开；比较固定预构建sysroot。 |
| 30 | 系统Python与GN检查裁剪 | 系统Python合理；移除circular include检查是源码裁剪配套，不应视作独立需求。 |
| 31 | 修复ThinLTO配置重复项 | 按用户要求暂时保留，与12维护；不是独立运行功能。 |
| 32 | URL测试/Java目标裁剪 | 有用；与01、06合并审查，避免重复补丁链。 |
| 33 | Perfetto tracing fuzz import裁剪 | 源码筛选配套；整合进统一裁剪。 |
| 34 | PartitionAlloc测试目标裁剪 | 源码筛选配套；不代表所有PartitionAlloc公共依赖可删。 |
| 35 | Perfetto service测试裁剪 | 源码筛选配套；整合。 |
| 36 | Perfetto protozero测试裁剪 | 源码筛选配套；整合。 |
| 37 | Perfetto filtering测试裁剪 | 源码筛选配套；整合。 |
| 38 | Perfetto protovm测试裁剪 | 源码筛选配套；整合。 |
| 39 | TPM依赖与Perfetto base测试删除 | 应拆分；TPM需Windows源文件闭包核对，Linux GN成功不足以支持跨平台结论。 |
| 40 | root cert重复源文件删除 | 由19引出；整理19后消除独立修补。 |
| 41 | protoc Java生成器删除 | 明确有用。35个C++编译单元；GN闭包无Java生成器，Python仍被proto构建调用。编译链接待完整验证。 |

## 高影响调用链

- `net/android/network_library_stub.cc` 的DNS查询返回false；
  `net/dns/host_resolver.cc` 在获取指定网络DNS失败时禁用builtin resolver。
  `net/dns/address_info.cc` 仅在指定NetworkHandle时调用专用解析，默认网络另走系统路径。
  因此不能把stub概括为“Android DNS不可用”，也不能认定行为与平台实现等价。
- `url/url_canon_icu_alternatives_dummy.cc` 的IDNToASCII无条件失败；
  `net/base/net_string_util_icu_alternatives_dummy.cc` 的多种转换同样失败。
- `base/time/time_exploded_posix.cc:19–55` 在32位Android上使用time64_t及64位转换函数；
  其他POSIX路径才使用time_t。14禁用的ICU补救只在sizeof(SysTime)<8时相关。
- `components/grpc_support/xray_engine.cc` 主动修改socket池及proxy-chain上限；
  21的必要性必须连同这些调用审查，不能只看patch本身很小。

### Android原生边界与证书来源

03的默认网络解析仍能走`net/dns/address_info.cc`中的`::getaddrinfo`，
不能将指定网络API的stub失败扩大为所有DNS失败。04的
`base/android/android_info_stub.cc:47`却将SDK版本固定为0；
`net/dns/host_resolver.cc`用SDK版本判断builtin resolver能力，因而这不只是删除设备描述信息。
默认解析可用也不证明网络切换通知、指定网络绑定或Android平台DNS行为完整。
当前引擎以host mapping连接给定地址，但另建的证书抓取context没有这条映射，
DNS裁剪仍须覆盖AIA等证书网络请求，不能只看主XHTTP连接。

在关闭NSS服务端证书支持的预期配置下，18并未把CA信任缩减为几个文件目录。`net/cert/cert_verify_proc.cc:416–425`
创建TrustStoreChrome，再交给`internal/system_trust_store.cc:495–498`，
与TrustStoreUnix合并。因此Chrome根库仍在，SSL_CERT_FILE/SSL_CERT_DIR只影响
附加文件来源，并不替换全部信任根。Android平台用户根的上游路径
`net/android/network_library.cc:42–49`调用Java；当前文件加载实现不能宣称保留了
用户/企业根、禁用根及平台更新的全部语义。是否需要这些语义尚无用户决定。

首次完整构建在system_trust_store.o暴露配置迁移遗漏：Chromium 156的
`crypto/features.gni`独立声明use_nss_client_certs和use_nss_server_certs，
仅关闭旧use_nss_certs时，这两个值在Linux仍为true。构建因缺少NSS头文件cert.h失败，
此时实际选择的是NSS分支，尚未到达上述文件信任库。build.sh现明确关闭两个开关，
使实现与原有无需NSS、使用文件附加根的构建意图一致；修正后的完整构建仍待完成。

13的IDN损失在当前入口可达：`components/grpc_support/xray_engine.cc:177`
直接用传入server_name构造GURL，Rust的`src/xhttp/cronet.rs`只转换CString，
没有在该边界做IDNA归一化。因而不能以“Rust已处理域名”为由认定dummy无影响。
这是源码结论，尚未执行Unicode域名的端到端测试。

## grpc_support审查

当前路径为Rust回调状态、C适配器、grpc_support::BidirectionalStream、net::BidirectionalStream。
必要职责是C ABI、网络线程投递、缓冲区生命周期、单次读写与取消/终止。
旧层还包含手动flush、延迟headers和三组WriteBuffers队列；Rust当前逐次写并等待完成，
不调用前述配置接口。这些能力没有已确认消费者。

直接实现net::BidirectionalStream::Delegate可减少一层delegate和未使用的批量写机制，
但仍必须实现读写结束状态和异步销毁，不能把删除旧状态机当作无需状态管理。
本轮不因旧适配器已通过编译而认定应保留，也不未经实现对照就认定重写一定更少。
下一步比较最小接口、所有权及终止事件映射，并与26/27的接入点一起决定。

`net/http/bidirectional_stream_request_info.h`当前没有隔离key或独立authority字段，
只有URL、extra_headers及target_network等。因此仅绕过grpc_support并不能消除26/27：
仍须在Chromium请求边界传入这些信息。可将26改成显式请求字段，避免私有header，
但这仍是需要随上游维护的补丁。27目前修改全局SPDY转换，影响面更大；
在确定局部接入方式前，不将“删除适配器”等同于“无需修改Chromium”。

审查建议是保留必要的线程、缓冲区和终止状态职责，比较直接Delegate实现，
不以grpc_support兼容性为约束保留未使用的批量写/flush接口。
本轮没有删除它；也没有将重写后的代码量或性能收益作为已验证事实。

## 文档与实现差异

- README旧“Applied patches”仍列强制隧道、CONNECT Fast Open、experimental proxy_server；
  当前series及相关net源码未发现这些扩展。当前engine使用host mapping和双向HTTP请求，
  不是CONNECT代理隧道。README顶部虽标明旧说明仅属原v119分发，仍不应据此判断当前能力。
- 12存在但src/build.sh固定use_thin_lto=false/is_cfi=false；生产LTO尚未配置完成。
- src/base/time/time_exploded_posix.cc注释仍说32位使用ICU，而14将该调用包在#if 0中。

## 收敛顺序

以下是实施建议，不将尚无跨平台证据的条件项直接删除：

1. 先修正新版NSS构建开关并完成Linux链接。该项是既有文件信任库方案的配置修复，
   不新增CA策略。Windows与Android必须分别编译，不能由Linux结果替代。
2. 对13优先比较恢复上游ICU路径，而不是新增自制IDNA或以ASCII限制代替现有域名能力。
   保留ICU会增加冷构建内容，这是实际取舍；其余文件名/字符集dummy是否可裁剪另按调用链判断。
   14随最终ICU选择处理，不单独为Android时间引入替代实现。
3. 对03–05保留无JVM目标，去除伪造平台信息的实现；先处理SDK版本和实际可达网络调用。
   没有公开JNI消费者不意味着内部平台调用可以无条件返回成功或默认值。
4. 对18–19先保留Chrome根库加文件附加根的现有方向，明确Android用户/企业CA与
   平台策略是否属于验收，再决定是否需要额外平台接入。pin仍是附加条件，不能替代CA验证。
5. 对grpc_support比较直接Delegate，保留单次读写、异步销毁和唯一终止通知；
   将隔离key传递收敛到显式请求字段。authority的局部接入点需单独确定，不能借重写扩大影响面。
6. 15、20、22、23是已识别的优先移除候选；28以实际Windows SDK环境判断。
   构建裁剪01、06、24–25、30、32–40在功能取舍明确后合并整理，12、31继续保留。

## 证据与限制

原生适配器三个对象编译成功：工作区运行目录
`../.codex/runs/cronet-upgrade/native-engine-03.log`、`native-stream-03.log`、
`native-c-adapter-02.log`。完整库日志`native-build-16-j6.log`仍在推进。
Java裁剪GN证据为`trim-java-gn.log`、`trim-java-deps.log`、`trim-java-impact.json`。
本审查未运行互通、生产LTO或Android/Windows验收，未删除任何补丁。
