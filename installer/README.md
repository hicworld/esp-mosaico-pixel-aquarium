# 分享包安装器

这里保存 v1.3.0 分享包中使用的 Windows 烧录工具源码。完整固件在 [Release 分享包](https://github.com/hicworld/esp-mosaico-pixel-aquarium/releases/tag/v1.3.0) 中，解压后进入 firmware 文件夹，按其中的烧录说明运行。

程序会核对 SHA256SUMS.json 中的四段固件，确认目标为 ESP32-S31，再以 115200 波特率通过 ROM USB 写入。默认鱼塘安装在 ota_0，会替换该位置的应用平台并重新设置 OTA 启动选择。

本目录中的 SHA256SUMS.json 对应首次发布的 1.3.0 固件。自行修改源码重建后，应重新生成摘要并核对启动文件与分区。
