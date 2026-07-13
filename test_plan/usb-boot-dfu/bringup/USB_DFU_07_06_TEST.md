# XIAO nRF54LM20B USB DFU 测试记录

本文档记录当前已经验证通过的 USB DFU 测试流程：

- 基线固件：`07-usb-loader-board-cdc`
- 更新固件：`06-usb-dfu-transfer-update`
- 启动链路：MCUboot firmware loader mode，USB CDC ACM + MCUmgr
- 测试工具：standalone nRF Util，不依赖完整 nRF Connect SDK 或本机 NCS 工具链

## 测试侧工具要求

测试电脑只需要安装以下工具和文件：

1. standalone `nrfutil.exe`。
2. `nrfutil` command：`device=2.17.5`。
3. `nrfutil` command：`mcu-manager=0.10.2`。
4. SEGGER J-Link 驱动，用于通过调试器烧录固件和写入 KMU key。
5. 本文档列出的测试固件文件。


## 下载 nRF Util

从 Nordic 官方页面下载 Windows 版本 standalone nRF Util：

```text
https://www.nordicsemi.com/Products/Development-tools/nRF-Util
```

建议测试电脑使用以下目录：

```text
C:\nrfutil\nrfutil.exe
C:\nrfutil\home
D:\xiao_nrf54lm20b_usb_dfu_test
```

## 安装 nRF Util Commands

打开 PowerShell，执行：

```powershell
$nrfutil = 'C:\nrfutil\nrfutil.exe'
$env:NRFUTIL_HOME = 'C:\nrfutil\home'

& $nrfutil install device=2.17.5 mcu-manager=0.10.2

& $nrfutil device --version
& $nrfutil mcu-manager --version
```

## 测试固件文件

将研发提供的 3 个文件放到同一个测试目录：

```text
D:\xiao_nrf54lm20b_usb_dfu_test
```

文件清单：

```text
D:\xiao_nrf54lm20b_usb_dfu_test\06-USB-DFU.hex
D:\xiao_nrf54lm20b_usb_dfu_test\dfu_application.zip
D:\xiao_nrf54lm20b_usb_dfu_test\keyfile.json
```

## PowerShell 公共变量

后续命令统一使用以下变量。

需要按实际测试环境修改：

- `$serialNumber`：J-Link 序列号。
- `$dfuCom`：进入 USB DFU 模式后枚举出来的 COM 口。

```powershell
$nrfutil = 'C:\nrfutil\nrfutil.exe'
$env:NRFUTIL_HOME = 'C:\nrfutil\home'

$serialNumber = '69660778'
$dfuCom = 'COM22'

$firmwareDir = 'D:\xiao_nrf54lm20b_usb_dfu_test'
$baselineHex = "$firmwareDir\06-USB-DFU.hex"
$keyFile = "$firmwareDir\keyfile.json"
$dfuZip = "$firmwareDir\dfu_application.zip"
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

连接 J-Link 后，执行：

```powershell
& $nrfutil device program `
  --firmware $baselineHex `
  --serial-number $serialNumber `
  --family nrf54l `
  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ
```


## 写入 KMU 密钥

签名校验用的 key 不在 `06-USB-DFU.hex` 中。
它通过 `keyfile.json` 写入芯片的 KMU key slot。

以下场景需要执行本步骤：

- 新板子首次烧录。
- 执行过 full erase 或 recover。
- 修改过签名 key。

命令如下：

```powershell
& $nrfutil device x-provision-keys `
  --key-file $keyFile `
  --serial-number $serialNumber `
  --family nrf54l

& $nrfutil device reset `
  --serial-number $serialNumber `
  --family nrf54l
```

如果同一个 KMU key 已经写入，并且芯片没有经过会清除 KMU 的 recover 或 erase 操作，通常不需要重复写入。

## 进入 USB DFU 模式

1. 按住 XIAO Button 0。
2. 复位板子。
3. 复位后松开 Button 0。
4. 确认应用层 LED 不闪烁。
5. 确认 Windows 枚举出 Zephyr CDC ACM COM 口。

查看 COM 口：

```powershell
Get-CimInstance Win32_SerialPort |
  Select-Object DeviceID, Name, PNPDeviceID
```

将实际 COM 口写入变量：

```powershell
$dfuCom = 'COM22'
```

## 检查 MCUmgr 连接

```powershell
& $nrfutil mcu-manager serial image-list `
  --serial-port $dfuCom `
  --timeout 60
```

该命令必须能够正常返回 image list，然后再执行上传。

## 通过 USB DFU 上传 06 更新包

```powershell
& $nrfutil mcu-manager serial image-upload `
  --serial-port $dfuCom `
  --timeout 60 `
  --firmware $dfuZip
```

复位后确认应用行为已经变成 06 固件：

- 红色 LED，也就是 `led1`，每 500 ms 翻转。


## 官方参考

- nRF Util 下载页：`https://www.nordicsemi.com/Products/Development-tools/nRF-Util`
- nRF Util device command：`https://docs.nordicsemi.com/r/bundle/nrfutil/page/nrfutil-device/guides/programming.html`
- nRF Util offline bundle：`https://docs.nordicsemi.com/r/bundle/nrfutil/page/guides/installing.html/preparing-nrf-util-offline-bundle`
- nRF Util commands offline install：`https://docs.nordicsemi.com/r/bundle/nrfutil/page/guides/installing.html/installing-nrf-util-commands-when-offline`
