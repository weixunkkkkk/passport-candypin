# 第三方来源与许可

## 随仓库分发的上游内容

| 来源 | 本项目用了什么 | 许可与文件 |
| --- | --- | --- |
| FoloToy AI Passport 开发资源，基线 `488beda41309f052d81f08d0016d65a3fd85eb7e` | ESP32-C3 构建骨架、8 MB 分区布局、SPI LCD、ST7789 初始化、背光、LVGL 适配、三键 ADC 输入、I²C、电量计和板级音频代码。合并镜像检查工具也来自上游。 | [上游固定版本](https://github.com/FoloToy/ai-passport/tree/488beda41309f052d81f08d0016d65a3fd85eb7e) · [上游 MIT 许可](https://github.com/FoloToy/ai-passport/blob/488beda41309f052d81f08d0016d65a3fd85eb7e/LICENSE) · [本仓库板级代码](components/bsp) |
| Wanli New Energy 电池参数 | FoloToy 板级代码中的 CW2017 500 mAh profile2 参数。固件电量显示使用上游的电量计实现和参数。 | [随上游 BSP 提供的参数记录](assets/battery/wanli-500mah-profile2-xil40x-20261020.txt) |
| Adobe Source Han Sans SC Medium（思源黑体） | 从原字体生成的中文 LVGL 位图字形 `snake_font`。生成时使用 LVGL 的 `lv_font_conv`，编译直接使用已生成的 C 文件。 | [字体上游](https://github.com/adobe-fonts/source-han-sans) · [转换工具](https://github.com/lvgl/lv_font_conv) · [随字形分发的 OFL 1.1](assets/fonts/power-snake/LICENSE.txt) |

BSP 的修改集中在屏幕休眠。复用已有 ST7789 与背光支持，新增运行时液晶休眠和唤醒函数，应用层按息屏设置暂停、恢复 LVGL 刷新。v0.5.5 的省电流程复用上游 BSP 的电量计休眠、ES8311 休眠与寄存器检查、I²S/I²C 关闭、按键深睡准备和屏幕 GPIO 保持能力。唤醒键保护状态机 [key_guard.h](main/key_guard.h) 及其 [测试](tests/test_key_guard.c) 也来自上述固定版本的 FoloToy 上游。应用层新增息屏策略、网络停止与恢复、深睡条件检查和旧设置迁移。当前应用没有播放语音或游戏音乐，音频 BSP 在启动后被显式置为休眠。

## 构建时取得的依赖

版本以 [依赖锁文件](dependencies.lock) 为准。下列依赖的源码由 ESP-IDF 和组件管理器提供。

| 依赖 | 版本 | 使用位置 | 上游许可 |
| --- | --- | --- | --- |
| [ESP-IDF](https://github.com/espressif/esp-idf) | 5.5.3 | FreeRTOS、Wi-Fi、HTTP/HTTPS、NVS、USB Serial/JTAG、定时器、LCD 驱动、SNTP、动态降频、自动浅睡与 GPIO 深睡唤醒等系统能力 | ESP-IDF 主体 Apache-2.0，各内含组件沿用自己的许可 |
| [cJSON](https://github.com/DaveGamble/cJSON) | 由 ESP-IDF 5.5.3 提供 | 解析局域网五口数据和 MCP JSON-RPC 响应 | MIT |
| [LVGL](https://components.espressif.com/components/lvgl/lvgl/versions/9.5.0) | 9.5.0 | 功率卡、设置菜单、像素图和屏幕渲染 | MIT |
| [espressif/esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port/versions/2.9.0) | 2.9.0 | LVGL 与 ESP-IDF 显示接口的适配 | Apache-2.0 |
| [espressif/button](https://components.espressif.com/components/espressif/button/versions/4.2.0) | 4.2.0 | 上游 BSP 的 ADC 按键与长短按事件 | Apache-2.0 |
| [espressif/esp_codec_dev](https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.6.2) | 1.6.2 | 上游 BSP 的音频编解码依赖 | Apache-2.0 |
| [espressif/cmake_utilities](https://components.espressif.com/components/espressif/cmake_utilities/versions/1.1.1) | 1.1.1 | 组件带入的构建依赖 | Apache-2.0 |
| [espressif/mdns](https://components.espressif.com/components/espressif/mdns/versions/1.14.1) | 1.14.1 | 在已连接的局域网上发现兼容的小电拼 CP-02S | Apache-2.0 |

## 设备接口与玩法参考

- **制糖工厂 AI 小电拼**提供功率数据接口。这里的 [客户端](main/snake_fetch.c) 读取局域网 `/metrics.json`，或按 MCP 的 HTTP 传输约定调用 `get_port_details`。设备端接口由对应的小电拼或 MCP 服务提供。[制糖工厂应用](https://apps.apple.com/cn/app/id6743526385) · [MCP 传输规范](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports)
- **CoCanMirror Passport**提供手机连接热点后打开配网页的交互参考。[参考配网流程](https://github.com/Kiiiiro/CoCanMirror-passport/blob/main/main/provisioning.c) · [CandyPin 热点服务](main/snake_portal.c)
- **玩法参考**。充电蛇参考经典 Snake，跳砖吃币参考《超级马力欧》式顶砖与越障，叠方块参考 Tetris 的下落消行，迷宫吃豆参考 Pac-Man，星舰防线参考 Space Invaders。此仓库的角色和背景用代码绘制，地图和规则由本项目实现。演示图使用模拟功率和示例像素画。[游戏实现](main/snake_games.c) · [场景绘制](main/snake_arcade.c)
- **本项目新增**。五口功率采集与换源、功率动画和接力演示、五款游戏、设备端设置、热点上传页、浏览器内像素转换、三画位存储和拼接动画。入口在 [应用代码](main/snake_app.c)，网络和上传分别在 [网络模块](main/snake_net.c) 与 [热点服务](main/snake_portal.c)。

AI Passport、小电拼及所列游戏名称用于说明兼容性和玩法来源。本仓库是独立项目。
