# Chromium native transport patch review

## 当前方向：最少上游修改

用户进一步确认：优先控制升级维护成本。ICU等裁剪只影响构建成本和体积，
不作为必须维护的能力；恢复上游实现优先于维护替代代码。
当前正在独立暂存目录验证新基线，尚未完成安装或GN配置验证，构建保持停止。
下面旧审查保留为前一阶段记录；与此节冲突的“保留裁剪”决定已被覆盖。

| 类别 | 原编号 | 新基线处理 |
|---|---|---|
| 必要嵌入改造 | 03–07 | 保留无JVM Android接入；不把这些stub与构建裁剪混为一类。 |
| 必要信任与连接语义 | 18、21、26、27 | 保留系统根接入、池上限及显式隔离/authority字段；21的proxy-chain部分仍需核对。 |
| 工具链/独立构建兼容 | 08、09、25、28、29 | 暂列条件项；保留不代表永久必需，应由对应平台的实际失败决定。 |
| 测试/目标裁剪 | 01、32–39 | 从候选series移除。保留上游GN文件，使用--root-target与--root-pattern限制到本地库依赖闭包。 |
| 替代实现及体积优化 | 02、13、14、16、17、41 | 从候选series移除。恢复Chromium Rust、ICU、libc++及protoc原实现；不维护自制替代代码。 |
| 全局编译配置修改 | 10、11、12、31 | 从候选series移除。保留默认日期宏和builtins逻辑；生产LTO优化放在本地共享库目标，不再改全局默认config。 |
| verifier默认值 | 19 | 从候选series移除。用chrome_root_store_only/optional及NSS参数选择builtin verifier；验证上游源文件选择是否足够。 |
| 精简GN启动文件 | 30 | 从候选series移除。使用上游.gn与--script-executable=python3，不再删除circular include检查。 |

候选series为14项，原补丁文件暂保留到新基线验证完成；不能将候选数量当作最终结果。
源码准备入口为tools/prepare-source.sh，完整解包发布源码、依次应用补丁并复制本地适配文件；
各CI矩阵任务独立准备源码。本地src/BUILD.gn不再特殊保留，
根目标通过GN命令选择。get-clang.sh改用上游Rust updater取得匹配工具链。
生产LTO仍保留，build.sh的THIN_LTO可显式调整，不以删除全局补丁为由删除优化要求。

## 前一阶段审查记录

审查及重构日期：2026-10-27。下表保留原41项编号，当前series剩36个补丁。
用户已要求先停止构建、裁剪补丁和接口，重构完成后重启。旧grpc_support适配层已移除，
改用components/xray_native直接连接Chromium Delegate；Windows、Android尚无构建或运行结论。

边界：最终消费者是 hray-core，Android 也由 Rust 核心调用 C ABI，不提供 Java/JNI
消费接口。Chromium 内部对 JNI 的依赖仍需处理。Windows、Linux、Android、CA+pin、
原生连接池及固定隔离分组必须保留。生产 LTO 有价值，12、31 按用户决定暂时保留。
用户后续确认：XHTTP场景继续裁剪ICU；支持系统CA加pin即可，不要求额外安装的
用户/企业CA。后者是支持范围，不要求主动排除操作系统信任库中已有的证书。

## 逐项结论

“条件保留”表示当前实现依赖它，不表示上游没有更简单的替代。已删除项保留编号用于追溯。

| 编号 | 内容 | 审查结论与依据 |
|---|---|---|
| 01 | 删除测试和无关构建目标 | 保留裁剪目标，重整实现。跨组件大补丁与32–39重叠；删除不可达测试目标不等于减少相同数量的实际编译任务。 |
| 02 | 移除 Chromium Rust，替换 JSON 等实现 | 待比较。减少跨平台工具链，但维护替代解析器和 structured headers 路径；hray-core 本就使用 Rust，不能以“不用 Rust”为必要性依据。 |
| 03 | Android 网络 stub | 条件保留纯原生接入能力，收缩 stub。DNS配置查询返回false、网络绑定返回ERR_NOT_IMPLEMENTED、指定网络解析返回EAI_SYSTEM；不能声称平台语义完整。 |
| 04 | Android base stub | 条件保留；sdk_int改为读取原生系统属性，删除已无构建引用、引用已移除头文件的build_info_stub。其余平台语义仍需逐函数核对。 |
| 05 | jni_zero 去 JNI | 当前无JVM实现条件保留；应仅覆盖可达JNI依赖。 |
| 06 | URL Android/Java裁剪 | 有用，但与32重叠；整合后不保留重复修改。 |
| 07 | 禁用Java模板 | 符合纯原生Android交付；保留。 |
| 08 | Android不额外链接libatomic | 工具链条件项，待当前NDK/Clang链接确认；不能仅沿用旧故障。 |
| 09 | 独立构建与NDK版本配置 | 独立构建有用；NDK固定值应另行核对，避免把构建模式和工具链版本绑在一起。 |
| 10 | 官方构建清除日期宏 | 非必要运行功能；可复现构建有价值但尚非明确验收，建议恢复上游或采用现成配置。 |
| 11 | 禁止显式链接Clang builtins | 条件项；与02耦合，需三平台链接证据再判断。 |
| 12 | ThinLTO最大优化 | 按用户要求暂时保留；当前build.sh关闭LTO，生产配置尚待落实，不能宣称生产优化已启用。 |
| 13 | ICU移除与dummy替代 | 保留。用户接受XHTTP场景裁剪ICU；IDNToASCII和字符集转换直接false，当前入口使用ASCII域名或预先转换的punycode，不增加替代IDNA实现。 |
| 14 | 移除ICU的32位时间处理 | 与13耦合。32位Android明确使用time64_t及mktime64/gmtime64_r，不存在此前推测的32位SysTime问题；风险限于实际使用窄SysTime的平台。不能据此要求Android恢复ICU，也不能据此证明整个ICU移除安全。 |
| 15 | 删除未知CPU的#error | 已删除，恢复上游未知CPU检查；当前目标CPU不依赖它。 |
| 16 | libc++/abi无异常与RTTI裁剪 | 可选优化，不是协议要求；应与17成组比较收益与维护成本。 |
| 17 | Windows无异常标准库修复 | 仅16保留时条件需要；尚无Windows编译证据。 |
| 18 | 自行读取Unix/Android CA文件 | 保留系统CA文件接入方向。用户不要求额外安装的CA，无需为此恢复Android Java用户根接口；系统根目录及三平台实际覆盖仍需核对。 |
| 19 | builtin verifier选择 | 保留builtin verifier选择，去除重复插入test_root_certs_builtin的修改，40不再需要。 |
| 20 | PKCS#7 AIA扩展 | 已删除，恢复上游AIA解析；CA验证仍然必需。 |
| 21 | 放宽连接池上限检查 | 当前引擎设置2048/2040直接依赖它；需核对代理链上限是否与直连XHTTP路径相关，固定分组并不自动证明所有上限修改必要。 |
| 22 | H2无效流日志降级 | 已删除，恢复上游日志级别。 |
| 23 | RST前插入填充DATA | 已删除，恢复上游RST_STREAM帧和流控行为。 |
| 24 | Brotli sanitizer import | 当前精简GN上下文需要；保留或并入构建裁剪。 |
| 25 | 独立Perfetto目标调整 | 当前构建条件保留；需与01和33–39一起整理。 |
| 26 | 双向流隔离key | 保留并重写。C ABI显式传入隔离site，转换成请求字段中的NetworkIsolationKey/NetworkAnonymizationKey，不使用私有header。 |
| 27 | HTTP authority与TLS名称分离 | 保留并重写。Host转换成显式authority请求字段，SPDY/H3转换只在字段非空时覆盖authority；无需私有header或全局header保留名单。 |
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
| 40 | root cert重复源文件删除 | 已删除；19不再重复插入源文件，消除先加后删。 |
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
- `components/xray_native/engine.cc` 主动修改socket池及proxy-chain上限；
  21的必要性必须连同这些调用审查，不能只看patch本身很小。

### Android原生边界与证书来源

03的默认网络解析仍能走`net/dns/address_info.cc`中的`::getaddrinfo`，
不能将指定网络API的stub失败扩大为所有DNS失败。04的
`base/android/android_info_stub.cc`原先将SDK版本固定为0，现改为原生读取
ro.build.version.sdk；`net/dns/host_resolver.cc`用该版本判断builtin resolver能力。
默认解析可用也不证明网络切换通知、指定网络绑定或Android平台DNS行为完整。
当前引擎以host mapping连接给定地址，但另建的证书抓取context没有这条映射，
DNS裁剪仍须覆盖AIA等证书网络请求，不能只看主XHTTP连接。

在关闭NSS服务端证书支持的预期配置下，18并未把CA信任缩减为几个文件目录。`net/cert/cert_verify_proc.cc:416–425`
创建TrustStoreChrome，再交给`internal/system_trust_store.cc:495–498`，
与TrustStoreUnix合并。因此Chrome根库仍在，SSL_CERT_FILE/SSL_CERT_DIR只影响
附加文件来源，并不替换全部信任根。Android平台用户根的上游路径
`net/android/network_library.cc:42–49`调用Java；当前文件加载实现不能宣称保留了
用户/企业根、禁用根及平台更新的全部语义。用户已确认不要求额外安装的CA，
因此不为用户/企业根增加JNI接入；系统CA来源的实际覆盖仍属于交付要求。

首次完整构建在system_trust_store.o暴露配置迁移遗漏：Chromium 156的
`crypto/features.gni`独立声明use_nss_client_certs和use_nss_server_certs，
仅关闭旧use_nss_certs时，这两个值在Linux仍为true。构建因缺少NSS头文件cert.h失败，
此时实际选择的是NSS分支，尚未到达上述文件信任库。build.sh现明确关闭两个开关，
使实现与原有无需NSS、使用文件附加根的构建意图一致；修正后的完整构建仍待完成。

13的IDN损失在当前入口可达：`components/xray_native/engine.cc:177`
直接用传入server_name构造GURL，Rust的`src/xhttp/cronet.rs`只转换CString，
没有在该边界做IDNA归一化。因而不能以“Rust已处理域名”为由认定dummy无影响。
这是源码结论，尚未执行Unicode域名的端到端测试。

## 原生流接口重构

当前路径为Rust回调状态、xray_native中的Stream、net::BidirectionalStream。
C ABI仍负责网络线程投递、缓冲区生命周期、单次读写、取消和终止；旧的中间Delegate、
三组WriteBuffers队列、手动flush、延迟headers及旧grpc测试/元数据均已删除。

公开操作缩减为六个xray_stream_*函数。移除优先级参数（固定为原调用值MEDIUM）、
未使用的协议字符串和trailers回调、header array的capacity、未实现的is_done声明。
成功、失败、取消统一为done(error)，0为成功，其他值为Chromium错误码。
Rust和原生库同步改ABI，新符号使误加载旧库在解析符号阶段失败，不保留兼容层。

每个流独立保留一个读buffer和一个写buffer，写入立即调用SendvData；
两侧EOF且最终写完成后才成功。终止时先使排队操作失效，再投递网络线程完成任务，
删除底层流、停止I/O后才通知Rust释放buffer。终止回调后异步删除delegate，
避免在Chromium回调栈中销毁调用方。context由原生流持有引用直到自身销毁。

26向BidirectionalStreamRequestInfo添加两个隔离字段；27向它及HttpRequestInfo
添加authority字段，再于SPDY/H3头转换处读取。保留这两项最小上游补丁，
不再用-network-isolation-key/-xray-authority伪装控制信息。

## 文档与实现差异

- README旧“Applied patches”仍列强制隧道、CONNECT Fast Open、experimental proxy_server；
  当前series及相关net源码未发现这些扩展。当前engine使用host mapping和双向HTTP请求，
  不是CONNECT代理隧道。README顶部虽标明旧说明仅属原v119分发，仍不应据此判断当前能力。
- 12存在但src/build.sh固定use_thin_lto=false/is_cfi=false；生产LTO尚未配置完成。
- 时间路径中已删除失效的ICU注释、禁用分支及声明；14不再保留#if 0或if(false)块。

## 收敛顺序

以下是实施建议，不将尚无跨平台证据的条件项直接删除：

1. 先修正新版NSS构建开关并完成Linux链接。该项是既有文件信任库方案的配置修复，
   不新增CA策略。Windows与Android必须分别编译，不能由Linux结果替代。
2. 按用户决定保留13的ICU裁剪，不恢复ICU或新增自制IDNA；明确当前域名入口需要ASCII或punycode。
   14作为配套保留，不单独为Android时间引入替代实现。
3. 对03–05保留无JVM目标，去除伪造平台信息的实现；先处理SDK版本和实际可达网络调用。
   没有公开JNI消费者不意味着内部平台调用可以无条件返回成功或默认值。
4. 对18–19保留Chrome根库加系统文件根的现有方向，不增加用户/企业CA支持。
   核对三平台系统根来源；pin仍是附加条件，不能替代CA验证。
5. 直接Delegate和显式隔离/authority字段已实现，后续编译及互通验证它们的事件和所有权边界。
6. 15、20、22、23、40已移除；28及其余工具链条件项保留到对应平台取得证据。
   构建裁剪仍按既有应用顺序维护，不为减少编号而改变存在上下文依赖的补丁顺序。12、31继续保留。

## 证据与限制

重构前原生适配器三个对象曾编译成功（不能作为新实现通过证据）：工作区运行目录
`../.codex/runs/cronet-upgrade/native-engine-03.log`、`native-stream-03.log`、
`native-c-adapter-02.log`。16因旧NSS开关迁移遗漏失败；17按用户要求主动中断，日志native-build-17-j6.log。
Java裁剪GN证据为`trim-java-gn.log`、`trim-java-deps.log`、`trim-java-impact.json`。
重构后桥接对象编译通过：native-stream-refactor-01.log；Rust check --tests和fmt通过：
check-native-05/cargo.log、fmt.log。补丁解析、反向应用检查、C头文件及shell语法检查见
refactor-01/static.log，条件和差异见同目录conditions.json、worktree.patch。
上述路径均相对../.codex/runs/cronet-upgrade。完整库链接、互通、生产LTO及Android/Windows验收仍未完成。
