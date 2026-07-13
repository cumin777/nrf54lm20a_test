## XIAO nRF54LM20B USB DFU 测试记录

本文档面向测试侧执行 USB DFU。测试人员安装好 nRF Util Commands 后，DFU 阶段只需要运行脚本并输入 COM 口。

## 测试侧工具要求

测试电脑只需要安装以下工具和文件：

1. standalone `nrfutil.exe`。
2. `nrfutil` command：`device=2.17.5`。
3. `nrfutil` command：`mcu-manager=0.10.2`。
4. SEGGER J-Link 驱动，用于通过调试器烧录固件和写入 KMU key。
5. 本文档列出的测试固件文件和 DFU 脚本。


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

将研发提供的文件放到同一个测试目录：

```text
D:\xiao_nrf54lm20b_usb_dfu_test
```

文件清单：

```text
D:\xiao_nrf54lm20b_usb_dfu_test\06-USB-DFU.hex
D:\xiao_nrf54lm20b_usb_dfu_test\dfu_application.zip
D:\xiao_nrf54lm20b_usb_dfu_test\keyfile.json
D:\xiao_nrf54lm20b_usb_dfu_test\run_usb_dfu.ps1
D:\xiao_nrf54lm20b_usb_dfu_test\run_usb_dfu.cmd
```

## 脚本依赖说明

`run_usb_dfu.cmd` 和 `run_usb_dfu.ps1` 不依赖预先设置 PowerShell 变量。

测试人员只需要确认：

1. nRF Util Commands 已安装完成。
2. `dfu_application.zip` 和脚本在同一个目录。
3. 板子已经进入 USB DFU 模式。
4. Windows 已枚举出 DFU COM 口，例如 `COM22`。

脚本会自动查找 `nrfutil.exe`：

1. 优先使用脚本同目录下的 `nrfutil.exe`。
2. 其次使用 `C:\nrfutil\nrfutil.exe`。
3. 最后从系统 `PATH` 中查找。

脚本会自动处理 `NRFUTIL_HOME`：

1. 如果存在 `C:\nrfutil\home`，脚本会使用该目录。
2. 如果不存在，脚本让 nRF Util 使用自己的默认 home。
3. 测试人员不需要手动设置 `$env:NRFUTIL_HOME`。

## 预期现象

`\06-USB-DFU.hex`：

- 正常复位：进入应用，`led2` 每 250 ms 翻转。
- 按住 Button 0 复位：MCUboot 进入 USB firmware loader mode。
- 进入 firmware loader mode 后，应用层 LED 不闪烁。
- Windows 枚举出 Zephyr CDC ACM 串口，例如：

```text
COM22    USB 串行设备 (COM22)    USB\VID_2FE3&PID_0004&MI_00\...
```

`dfu_application.zip` 通过 DFU 更新成功后：

- 应用镜像版本：`0.0.1+0`。
- 应用运行后 红色`led1` 每 500 ms 翻转。

## 烧录固件 

jlink烧录：06-USB-DFU.hex

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

## 一键执行 USB DFU

推荐测试人员使用脚本执行 DFU。脚本会自动使用同目录下的 `dfu_application.zip`，并依次执行：

1. `image-list` 检查连接。
2. `image-upload` 上传更新包。
3. `reset` 复位设备。

操作步骤：

1. 确认板子已经进入 USB DFU 模式。
2. 确认 Windows 已枚举出 DFU COM 口。
3. 执行：

```powershell
cd D:\xiao_nrf54lm20b_usb_dfu_test
.\run_usb_dfu.cmd
```

4. 按提示输入 COM 口，例如：

```text
COM22
```

脚本执行完成后，确认应用行为已经变成更新后固件：

- 红色 LED，也就是 `led1`，每 500 ms 翻转。

## 官方参考

- nRF Util 下载页：`https://www.nordicsemi.com/Products/Development-tools/nRF-Util`
- nRF Util device command：`https://docs.nordicsemi.com/r/bundle/nrfutil/page/nrfutil-device/guides/programming.html`
- nRF Util offline bundle：`https://docs.nordicsemi.com/r/bundle/nrfutil/page/guides/installing.html/preparing-nrf-util-offline-bundle`
- nRF Util commands offline install：`https://docs.nordicsemi.com/r/bundle/nrfutil/page/guides/installing.html/installing-nrf-util-commands-when-offline`
