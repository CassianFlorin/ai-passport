<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### Cassian 首页

- `fonts/geist-latin.woff2` 与 `fonts/instrument-serif-latin.woff2` 是从用户
  `cassian-next-blog` 构建缓存取得的原始拉丁字体文件（2026-10-26），未作修改。
  上游字族为 [Geist](https://github.com/vercel/geist-font) 和
  [Instrument Serif](https://github.com/Instrument/instrument-serif)。
  版权声明与 SIL Open Font License 1.1 保存在
  [`fonts/OFL.txt`](fonts/OFL.txt)；生成子集同样遵循该许可。
- `fonts/cassian_display_28.c` 为 Geist 600 字重、28 px、4 bpp 子集，
  包含大写拉丁字母、数字、空格、`.`、`-` 与 `%`。
  `fonts/cassian_editorial_18.c` 为 Instrument Serif 常规体、18 px、4 bpp
  子集，覆盖 `CF`、`Building things.`、`Mapping ideas.`、
  `Leaving traces.` 中的字母与标点。
- `fonts/jetbrains-mono-latin.woff2` 是来自同一主站构建缓存的 JetBrains Mono
  可变拉丁字体原文件，版本 2.211，未作修改。上游为
  [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono)；
  版权声明与 SIL Open Font License 1.1 保存在
  [`fonts/JetBrainsMono-OFL.txt`](fonts/JetBrainsMono-OFL.txt)。
  `fonts/cassian_mono_12.c`、`fonts/cassian_mono_14.c` 与
  `fonts/cassian_mono_32.c` 各含 95 个可打印 ASCII 字形，400 字重、
  4 bpp，用于仪表标签与数值读数，位图合计占 21,228 字节 Flash。
- [`../tools/generate_cassian_assets.py`](../tools/generate_cassian_assets.py)
  将保留的源文件转换为 LVGL 9 未压缩字形位图与驻留 Flash 的 RGB565 肖像。
  本次再生成使用 fontTools 4.63.0、sharp 0.35.5 及其内置 SVG 渲染器：

  ```bash
  python3 tools/generate_cassian_assets.py --sharp-module /path/to/node_modules/sharp
  ```

  编译生成的 C 文件不需要这些开发依赖。肖像含 72,576 字节 RGB565 像素。
  当前中文应用链接 Geist、三份等宽字体子集和下述三份 UI 子集；
  Instrument Serif 作为可复用资源保留，不参与链接。运行时仍使用 LVGL
  常规的分块绘制缓冲与 32 KB 内存池，不另分配完整帧缓冲。原生渲染通过了
  4,000 次页面切换，主机配置中内存池分配峰值为 18,928 字节；此结果不能
  证明实机堆内存余量。实机中文显示、对比度与内存余量仍需板端确认。

应用采用石墨色仪表布局，使用细网格、直线菜单边框、矢量图标、荧光绿
选中态与等宽读数。个人主页保留山景肖像，双击 OK 打开独立功能列表。
系统页仪表显示可用内存比例。设置页使用 20 段输出刻度，每段代表 5%，
静音时全部熄灭。网格、图标与刻度由页面拥有的 LVGL 回调绘制，
不为每条线或每段刻度创建对象，也不新增任务或计时器。预览遥测使用模拟
数据，固件读数来自既有 BSP 与堆内存 API。

### 简体中文界面

- `fonts/cassian-ui-source.woff2` 是 Noto Sans SC 常规体 `2.004-H2`
  的改名子集，源字体来自本机已安装的 `@larksuite/whiteboard-cli` 字体资源。
  原始 TTF 的 SHA-256 为
  `5cf8b2a0576d5680284ab03a7a8219499d59bbe981a79bb3dc0031f251c39736`。
  仓库仅保留 22,220 字节的子集。版权声明与 SIL Open Font License 1.1
  位于 [`fonts/NotoSansSC-OFL.txt`](fonts/NotoSansSC-OFL.txt)；字族改名为
  Cassian UI，保留原始许可元数据。
- `fonts/cassian-ui-characters.txt` 记录固定界面使用的 80 个非 ASCII 字符。
  `fonts/cassian_ui_14.c` 和 `fonts/cassian_ui_18.c` 在此基础上包含可打印
  ASCII，各有 175 个字形，分别为 14/18 px、4 bpp。
  `fonts/cassian_ui_28.c` 仅包含大号静音状态使用的 U+9759、U+97F3。
  它们是 LVGL 9 未压缩位图字库，不支持任意姓名或网络文本。未知字符不在
  支持范围内；保留缺字占位符，避免缺字静默消失。
- 转换脚本从 `main/personal_home.c` 中提取中文字符串。使用前述命令可以
  从保留的源子集再生成。需要新增字符时，提供许可允许使用的完整源字体，
  同时刷新源子集。请使用同一 `2.004-H2` 版本；其他版本的字形轮廓略有不同，
  会改变现有界面的全部字形。

  ```bash
  python3 tools/generate_cassian_assets.py \
    --sharp-module /path/to/node_modules/sharp \
    --chinese-source /path/to/NotoSansSC-Regular.ttf
  python3 tools/check_cassian_fonts.py
  ```

- 统一静态检查无需开发依赖，即可核对字符清单、各字库的覆盖范围、已知缺失
  字符 U+9F98、位图边界与等宽字体的统一前进宽度。原生 LVGL 验证还会按
  Unicode 码点解码每个标签的 UTF-8，检查实际选用字体、布局以及选中、静音
  和报错状态，并统计音量刻度中点亮的段数。中文 UI 位图共占 29,232 字节
  Flash，加上 Geist 与 JetBrains Mono 后，参与链接的固定界面字体位图共
  57,891 字节。菜单图标与音量刻度采用 LVGL 绘图原语，不需要图片解码或
  图标字体回退。

### AI 对话文字

- `fonts/cassian_chat_14.c` 用于显示语音识别文字、AI 回复，以及 AI 对话页
  （`main/ai_page.c`）上的全部固定文字。它包含可打印 ASCII、全部 7,445 个
  GB2312 字符（含 6,763 个汉字）和 `— – … · “ ” ‘ ’` 等标点：共 7,543 个
  字形，14 px、4 bpp，位图 679,301 字节，连同描述表约占 0.75 MB Flash。
  字形直接从 Flash 读取，不占用内部 RAM。表情符号、生僻字等集合外字符会被
  移除或显示为占位符。
- 来源：设备所有者提供的 Noto Sans SC 常规体较早版本
  `Version 0.00 July 30, 2017`（TTF SHA-256
  `a0af66489baf1f306732ff16da58d86c342e0b3b4b3674079ee2a9b0aee23258`），
  同样适用 [`fonts/NotoSansSC-OFL.txt`](fonts/NotoSansSC-OFL.txt) 中的
  SIL Open Font License 1.1。16 MB 的源字体不提交到仓库。单独再生成此字库
  不会改动其他资源：

  ```bash
  python3 tools/generate_cassian_assets.py \
    --sharp-module /path/to/node_modules/sharp \
    --chat-source /path/to/NotoSansSC-Regular.ttf
  python3 tools/check_cassian_fonts.py
  ```

- 静态检查会核对 GB2312 全覆盖，以及 AI 对话页的固定文字全部位于该集合内。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |
| [`images/cassian-avatar.png`](images/cassian-avatar.png) | 1024 × 1024，PNG | 取自 `cassianflorin.com` 本地源码的用户主站图像，按用户要求用于个人固件。原样保留，未提供单独的第三方再分发许可。 |
| [`images/cassian_portrait.c`](images/cassian_portrait.c) | 216 × 168，RGB565 C 数组 | 上一图像按屏幕尺寸居中裁切的版本，由 Cassian 资源转换脚本生成，直接从 Flash 读取。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。
