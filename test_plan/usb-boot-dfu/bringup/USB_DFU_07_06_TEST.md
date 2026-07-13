# XIAO nRF54LM20B USB DFU 测试记录

本文档记录当前已经验证通过的 USB DFU 流程：

- 基线固件：`07-usb-loader-board-cdc`
- 更新固件：`06-usb-dfu-transfer-update`
- 启动链路：MCUboot firmware loader mode，USB CDC ACM + MCUmgr

## 测试环境

研发机构建环境如下，仅用于说明固件来源；测试侧执行烧录和 USB DFU 不需要安装完整 nRF Connect SDK 或工具链。

```powershell
$tc = 'C:\ncs\toolchains\936afb6332'
$sdk = 'D:\workspace\ncs\v3.3.0'
$boardRoot = 'D:\workspace\xiao_nrf54lm20b\platform-seeedboards\zephyr'
$board = 'xiao_nrf54lm20b/nrf54lm20b/cpuapp'
```

## 测试侧最小工具安装

测试侧只需要以下内容：

1. standalone `nrfutil.exe`。
2. `nrfutil` commands：`device` 和 `mcu-manager`。
3. SEGGER J-Link 驱动，用于 `nrfutil device program` 和 `x-provision-keys` 通过调试器访问芯片。
4. 测试固件文件：`merged.hex`、`keyfile.json`、`dfu_application.zip`。

不需要安装完整的 NCS SDK，也不需要安装 `C:\ncs\toolchains\936afb6332`。

在线安装方式：

```powershell
$nrfutil = 'C:\Tools\nrfutil\nrfutil.exe'
$env:NRFUTIL_HOME = 'C:\Tools\nrfutil\home'

& $nrfutil install device=2.17.5 mcu-manager=0.10.2

& $nrfutil device --version
& $nrfutil mcu-manager --version
```

版本选择：

- `device=2.17.5`：本项目验证过 nRF54LM20B，可避免旧版本不识别 part number `0x00000033`。
- `mcu-manager=0.10.2`：本项目已验证 USB CDC ACM + MCUmgr DFU 正常。

离线安装方式：

在有网络的电脑上准备离线包：

```powershell
$nrfutil = 'C:\Tools\nrfutil\nrfutil.exe'
& $nrfutil prepare-offline D:\nrfutil-offline
```

将 `D:\nrfutil-offline` 和 `nrfutil.exe` 拷贝到测试电脑，然后执行：

```powershell
$nrfutil = 'D:\nrfutil\nrfutil.exe'
$env:NRFUTIL_HOME = 'D:\nrfutil\home'

& $nrfutil install --from-offline D:\nrfutil-offline device=2.17.5 mcu-manager=0.10.2

& $nrfutil device --version
& $nrfutil mcu-manager --version
```

参考官方文档：

- nRF Util 下载页：`https://www.nordicsemi.com/Products/Development-tools/nRF-Util`
- nRF Util device command：`https://docs.nordicsemi.com/r/bundle/nrfutil/page/nrfutil-device/guides/programming.html`
- nRF Util offline bundle：`https://docs.nordicsemi.com/r/bundle/nrfutil/page/guides/installing.html/preparing-nrf-util-offline-bundle`
- nRF Util commands offline install：`https://docs.nordicsemi.com/r/bundle/nrfutil/page/guides/installing.html/installing-nrf-util-commands-when-offline`

## 本机已验证工具路径

本机设备烧录和 KMU 密钥写入使用工具链自带的 nrfutil home：

```powershell
$env:NRFUTIL_HOME = "$tc\nrfutil\home"
```

USB MCUmgr 命令使用单独的 nrfutil home，其中已安装 `mcu-manager`：

```powershell
$env:NRFUTIL_HOME = 'D:\workspace\nrfutil_mcumgr_home'
```

## 固件路径

基线固件，用于 J-Link 或 `nrfutil device program` 直接烧录：

```text
D:\workspace\build_usb_bringup_07_usb_loader_board_cdc_b\merged.hex
```

基线固件对应的 KMU key 文件：

```text
D:\workspace\build_usb_bringup_07_usb_loader_board_cdc_b\keyfile.json
```

通过 USB DFU 上传的更新包：

```text
D:\workspace\build_usb_bringup_06_usb_dfu_transfer_update_b\dfu_application.zip
```

如果需要单独的 signed bin，可使用：

```text
D:\workspace\build_usb_bringup_06_usb_dfu_transfer_update_b\06-usb-dfu-transfer-update\zephyr\zephyr.signed.bin
```

## 预期现象

`07-usb-loader-board-cdc`：

- 正常复位：进入应用，`led2` 每 250 ms 翻转。
- 按住 Button 0 复位：MCUboot 进入 USB firmware loader mode。
- 进入 firmware loader mode 后，应用层 LED 不闪烁。
- Windows 枚举出 Zephyr CDC ACM 串口，例如：

```text
COM22    USB 串行设备 (COM22)    USB\VID_2FE3&PID_0004&MI_00\...
```

`06-usb-dfu-transfer-update` 通过 DFU 更新成功后：

- 应用镜像版本：`0.0.1+0`。
- 应用运行后 `led1` 每 500 ms 翻转。
- 当前 XIAO 板级 DTS 中，`led1` 对应红色 LED。

## 烧录基线固件

使用 `nrfutil device program`：

```powershell
$nrfutil = 'C:\Tools\nrfutil\nrfutil.exe'
$env:NRFUTIL_HOME = 'C:\Tools\nrfutil\home'

& $nrfutil device program `
  --firmware D:\workspace\build_usb_bringup_07_usb_loader_board_cdc_b\merged.hex `
  --serial-number 69660778 `
  --family nrf54l `
  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ
```

也可以使用 J-Link Commander：

```text
loadfile D:\workspace\build_usb_bringup_07_usb_loader_board_cdc_b\merged.hex
r
g
```

## 写入 KMU 密钥

签名校验用的 key 不在 `merged.hex` 中。
它通过 `keyfile.json` 写入芯片的 KMU key slot。

以下场景需要执行本步骤：

- 新板子首次烧录。
- 执行过 full erase 或 recover。
- 修改过签名 key。
- 不确定 KMU key slot 当前状态。

命令如下：

```powershell
$nrfutil = 'C:\Tools\nrfutil\nrfutil.exe'
$env:NRFUTIL_HOME = 'C:\Tools\nrfutil\home'

& $nrfutil device x-provision-keys `
  --key-file D:\workspace\build_usb_bringup_07_usb_loader_board_cdc_b\keyfile.json `
  --serial-number 69660778 `
  --family nrf54l

& $nrfutil device reset `
  --serial-number 69660778 `
  --family nrf54l
```

如果同一个 KMU key 已经写入，并且芯片没有经过会清除 KMU 的 recover 或 erase 操作，通常不需要重复写入。

## 进入 USB DFU 模式

1. 按住 XIAO Button 0。
2. 复位板子。
3. 复位后松开 Button 0。
4. 确认应用层 LED 不闪烁。
5. 确认 Windows 枚举出 Zephyr CDC ACM COM 口。

查看 COM 口示例：

```powershell
Get-CimInstance Win32_SerialPort |
  Select-Object DeviceID, Name, PNPDeviceID
```

预期 VID/PID：

```text
VID_2FE3&PID_0004
```

## 检查 MCUmgr 连接

将 `COM22` 替换为实际枚举出来的 DFU COM 口。

```powershell
$nrfutil = 'C:\Tools\nrfutil\nrfutil.exe'
$env:NRFUTIL_HOME = 'C:\Tools\nrfutil\home'

& $nrfutil mcu-manager serial image-list `
  --serial-port COM22 `
  --timeout 60
```

该命令必须能够正常返回 image list，然后再执行上传。

## 通过 USB DFU 上传 06 更新包

```powershell
$nrfutil = 'C:\Tools\nrfutil\nrfutil.exe'
$env:NRFUTIL_HOME = 'C:\Tools\nrfutil\home'

& $nrfutil mcu-manager serial image-upload `
  --serial-port COM22 `
  --timeout 60 `
  --firmware D:\workspace\build_usb_bringup_06_usb_dfu_transfer_update_b\dfu_application.zip
```

上传完成后复位：

```powershell
& $nrfutil mcu-manager serial reset `
  --serial-port COM22 `
  --timeout 60
```

复位后确认应用行为已经变成 06 固件：

- 红色 LED，也就是 `led1`，每 500 ms 翻转。

## 已验证结果

当前已验证通过的结果如下：

1. `07-usb-loader-board-cdc` 正常启动时，应用 LED 按原始逻辑闪烁。
2. 按住 Button 0 复位后，MCUboot 进入 USB firmware loader mode。
3. Windows 枚举出 `USB\VID_2FE3&PID_0004`。
4. `nrfutil mcu-manager serial image-list` 可以在 DFU COM 口正常返回。
5. 上传 `06-usb-dfu-transfer-update` 成功。
6. 复位后，新应用运行，红色 LED 每 500 ms 闪烁。
