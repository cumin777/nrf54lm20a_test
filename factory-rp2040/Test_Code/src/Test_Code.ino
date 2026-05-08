#include <Arduino.h>

// 引脚定义
#define BUTTON_PIN 7          // Key_input 按键引脚
#define ADC_VBUS_PIN 26       // 检测产品VBUS的ADC引脚
#define ADC_3V3_PIN 27        // 检测3V3电压的ADC引脚
#define Signal_Control 10     // 产品与主板通信的线路通断控制开关
#define BATTERY_SW 8          // 模拟电池的电源开关
#define XIAO_USB_SW 9         // 与XIAO通信的USB开关
#define BEEP 11               // 有源蜂鸣器控制引脚

// LED引脚定义
const int ledPins[4] = {2, 3, 4, 5}; // LED引脚
int ledState[4] = {0, 0, 0, 0};      // LED状态
volatile int testState = 0;          // 测试状态
volatile bool buttonPressed = false; // 按键按下标志
volatile bool buttonReleased = false; // 按键松开标志

unsigned long previousMillis = 0;    // LED闪烁计时
unsigned long ledBlinkInterval = 500; // LED闪烁间隔

bool lastButtonState = false;        // 上一次按键状态
bool flash_saved_state = false;        // flash测试结果保存

// 全局字符串常量
const char* SINK_ON_CMD = "SINKON$";
const char* SUPPLY_ON_CMD = "SUPPLYON$";
const char* GET_CURRENT_CMD = "GETCRNTSGL$";
const char* GET_SLEEP_CURRENT_CMD = "GETCRNTUASGL$";
const char* SINK_OFF_CMD = "SINKOFF$";
const char* SUPPLY_OFF_CMD = "SUPPLYOFF$";
const char* SLEEP_CMD = "Sleep";
const char*  DFLTMODE_ON_CMD = "DFLTMODEON$";
const char*  DFLTMODE_OFF_CMD = "DFLTMODEOFF$";

const char* bt_init ="bt init\r\n";
const char* bt_init_pass="LMP: version 6.0";
const char* bt_scan_on = "bt scan on\r\n";
const char* bt_scan_pass = "[DEVICE]";
const char* bt_scan_off = "bt scan off\r\n";

// 函数声明
bool readVoltageAnd3V3Test();
bool readChargingCurrentTest(float CHARGING_THRESHOLD);
bool readUARTTest();
bool readBattryCurrentTest();
void resetSystem();
void manageLEDs();
bool sendAndParse(const char *command, float &value, const char *expectedUnit, unsigned long timeout, int maxRetries = 3); // 默认参数只在声明中定义
bool Analog_charge_Init();
bool performTest(); // 声明 performTest 函数
void beep_xms(int x);

// 中断函数定义
void handleButtonRelease() {
  buttonReleased = true; // 标记按键松开
}

void setup() {
  // 初始化串口通信
  Serial.begin(115200);
  Serial1.begin(115200);
  Serial2.begin(115200);

  // 发送固件版本号
  Serial.println("Firmware Version 1.0");

  // 设置按键引脚和中断
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonRelease, FALLING);

  // 设置控制引脚
  pinMode(Signal_Control, OUTPUT);
  pinMode(BATTERY_SW, OUTPUT);
  pinMode(XIAO_USB_SW, OUTPUT);
  pinMode(BEEP, OUTPUT);

  // 设置ADC引脚
  pinMode(ADC_VBUS_PIN, INPUT);
  pinMode(ADC_3V3_PIN, INPUT);

  // 设置LED引脚
  for (int i = 0; i < 4; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  // 等待Serial2初始化完成
  while (!Serial2) {}

  Serial.println("Test System Initializing");
  digitalWrite(BATTERY_SW,HIGH);
  // 初始化模拟充电模块
  //Analog_charge_Init()
  if (true) {
    beep_xms(500);
    // LED闪烁表示初始化成功
    for (int j = 0; j < 3; j++) {
      for (int i = 0; i < 4; i++) {
        digitalWrite(ledPins[i], HIGH);
      }
      delay(500);
      for (int i = 0; i < 4; i++) {
        digitalWrite(ledPins[i], LOW);
      }
      delay(500);
    }
    
    Serial.println("Test System Init Ready");
  } else {
    Serial.println("Test System Init Failed");
    return;
  }
}

void beep_xms(int x)
{
  digitalWrite(BEEP, HIGH); // 蜂鸣器响一次
  delay(x);
  digitalWrite(BEEP, LOW);
}

bool Analog_charge_Init() {
  const char *commands[] = {
      DFLTMODE_ON_CMD
      // SINK_OFF_CMD,
      // SUPPLY_OFF_CMD,
      // "SETSPLYCRNTLMT=1000$",
      // "SETSINKCRNTLMT=1000$",
      // "SETVLTG=3700$",
      // SUPPLY_OFF_CMD
      };

  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    float value = 0;
    if (!sendAndParse(commands[i], value, nullptr, 5000)) {
      Serial.print("Command failed: ");
      Serial.println(commands[i]);
      return false;
    }
    delay(100);
  }

  Serial.println("Analog charge module initialized successfully.");
  return true;
}

bool sendAndParse(const char *command, float &value, const char *expectedUnit, unsigned long timeout, int maxRetries) {
  char response[1024]; // 用于存储响应的缓冲区
  int retryCount = 0;  // 重试计数器

  while (retryCount < maxRetries) {
    // 清空缓冲区
    memset(response, 0, sizeof(response));

    // 发送命令
    Serial2.write(command);
    
    // Serial2.flush(); // 确保命令完全发送
    Serial.print("Send: ");
    Serial.println(command);

    // 等待响应
    unsigned long startTime = millis();
    bool ackReceived = false; // 是否收到 ACK
    bool valueParsed = false; // 是否成功解析值

    while (millis() - startTime < timeout) {
      if (Serial2.available()) {
        // 读取一行数据
        size_t length = Serial2.readBytesUntil('\n', response, sizeof(response) - 1);
        response[length] = '\0'; // 确保字符串以 null 结尾
        Serial.print("Received: ");
        Serial.println(response);

        // 检查是否收到 ACK
        if (strstr(response, "ACK") != nullptr) {
          ackReceived = true;
          if (expectedUnit == nullptr) {
            return true; // 不需要解析值，直接返回成功
          }
        }

        // 解析响应中的值和单位
        float parsedValue = 0.0;
        char unit[8] = "";
        if (sscanf(response, "%f%7s", &parsedValue, unit) == 2) {
          if (expectedUnit == nullptr || strcmp(unit, expectedUnit) == 0) {
            value = parsedValue; // 解析成功，返回解析值
            valueParsed = true;
            return true;
          } else {
            Serial.print("Unexpected unit: ");
            Serial.println(unit);
            break; // 单位不匹配，退出重试
          }
        }
      }
      delay(10); // 等待更多数据
    }

    // 检查是否成功
    if (ackReceived || valueParsed) {
      return true; // 成功接收到 ACK 或解析到值
    }

    // 超时或单位不匹配，重试
    retryCount++;
    Serial.print("Retry ");
    Serial.print(retryCount);
    Serial.print(" of ");
    Serial.println(maxRetries);
    delay(500); // 重试前等待一段时间
  }

  // 达到最大重试次数，返回失败
  Serial.println("Max retries reached. Command failed!");
  return false;
}

/**
 * @brief 发送命令并等待响应，检查响应中是否包含特定字符串，并可选择性地在成功后发送一个结束命令。
 * 
 * @param command 要通过 Serial1 发送的初始命令字符串。
 * @param targetString 期望在响应中找到的目标字符串。
 * @param timeout 单次尝试等待响应的超时时间（毫秒）。
 * @param maxRetries 如果失败，最大重试次数。
 * @param endCommand (可选) 如果不为 NULL，在成功找到 targetString 后，会立即发送此命令。用于停止设备等操作。传入 NULL 则不执行任何操作。
 * @return bool 如果在任何一次尝试中成功找到目标字符串，则返回 true；否则返回 false。
 */
bool sendAndCheckResponse(const char* command, const char* targetString, unsigned long timeout, int maxRetries, const char* endCommand) {
  // 用于存储 Serial1 响应的缓冲区。
  char responseBuffer[1024]; 
  int retryCount = 0;

  while (retryCount < maxRetries) {
    if (buttonReleased) {
      Serial.println("Button Released! Exiting VBUS Test.");
      return false;
    }
    if (Serial1.available()) {
      Serial1.read();
    }
    // 1. 发送初始命令
    Serial.print("--> Attempt ");
    Serial.print(retryCount + 1);
    Serial.print("/");
    Serial.println(maxRetries);
    while (Serial1.available()) {
      Serial1.read();
    }
    Serial.print("    Sending initial command to Serial1: ");
    Serial.println(command);
    Serial1.println(command);

    // 2. 在超时时间内等待和解析响应
    unsigned long startTime = millis();
    while (millis() - startTime < timeout) {
      if (Serial1.available()) {
        memset(responseBuffer, 0, sizeof(responseBuffer));
        size_t length = Serial1.readBytesUntil('\n', responseBuffer, sizeof(responseBuffer) - 1);
        responseBuffer[length] = '\0';

        Serial.print("    Received from Serial1: ");
        Serial.println(responseBuffer);

        // 3. 检查响应中是否包含目标字符串
        if (strstr(responseBuffer, targetString) != NULL) {
          Serial.println("--> Success: Target string found in response.");

          // 4. (新功能) 如果定义了结束命令，则发送它
          if (endCommand != NULL) {
            Serial.print("    Sending end command to Serial1: ");
            Serial.println(endCommand);
            Serial1.println(endCommand); // 发送结束命令
          }

          return true; // 成功找到，函数返回 true
        }
      }
      // delay(20);
    }

    // 5. 本次尝试超时
    retryCount++;
    if (retryCount < maxRetries) {
        Serial.println("--> Timeout or target not found. Retrying...");
        delay(500);
    }
  }

  // 6. 所有重试都失败
  Serial.println("--> Command failed after all retries.");
  return false;
}

void manageLEDs() {
  unsigned long currentMillis = millis();
  for (int i = 0; i < 4; i++) {
    if (ledState[i] == 1) {
      // LED闪烁（测试失败）
      if (currentMillis - previousMillis >= ledBlinkInterval) {
        digitalWrite(ledPins[i], !digitalRead(ledPins[i]));
        previousMillis = currentMillis;
      }
    } else if (ledState[i] == 2) {
      // LED常亮（测试通过）
      digitalWrite(ledPins[i], HIGH);
    } else if (ledState[i] == 3) {
      if (currentMillis - previousMillis >= ledBlinkInterval) {
        digitalWrite(ledPins[0], !digitalRead(ledPins[0]));
        digitalWrite(ledPins[1], !digitalRead(ledPins[1]));
        digitalWrite(ledPins[2], !digitalRead(ledPins[2]));
        digitalWrite(ledPins[3], !digitalRead(ledPins[3]));
        previousMillis = currentMillis;
      }
      
    } else {
      // LED关闭（未测试或重置状态）
      digitalWrite(ledPins[i], LOW);
    }
  }
}

void resetSystem() {
  buttonPressed = false;
  buttonReleased = false;
  testState = 0;
  flash_saved_state = false;

  digitalWrite(BATTERY_SW, LOW);
  delay(400);
  digitalWrite(BATTERY_SW, HIGH);

  // 重置LED状态
  for (int i = 0; i < 4; i++) {
    ledState[i] = 0;
    digitalWrite(ledPins[i], LOW);
  }

  // 关闭所有控制引脚
  digitalWrite(Signal_Control, LOW);
  
  digitalWrite(XIAO_USB_SW, LOW);

  bool commandSuccess = false;
  float dummy = 0;
  if (sendAndParse(DFLTMODE_OFF_CMD, dummy, nullptr, 500,2)) {
    commandSuccess = true;
    Serial.print("Command succeeded: ");
    Serial.println(DFLTMODE_OFF_CMD);
    // break;
  } else {
    Serial.print("Command failed (retrying...): ");
    Serial.println(DFLTMODE_OFF_CMD);
    delay(200);
  }
  if (!commandSuccess) {
      Serial.print("Command failed after maximum retries: ");
      Serial.println(DFLTMODE_OFF_CMD);
      Serial.println("Battery module initialization failed!");
      return;
    }
  

  Serial.println("Battery module re-initialized successfully.");
  Serial.println("System reset completed.");
}

////////////////////////////////////////////////////////////////////////////////////
// 合并VBUS和3V3检测
bool readVoltageAnd3V3Test() {
  digitalWrite(Signal_Control, HIGH);  //打开线路开关检测电压和UART通信
  

  unsigned long startMillis = millis();

  // 检测VBUS电压
  while (millis() - startMillis < 30000) {
    if (buttonReleased) {
      Serial.println("Button Released! Exiting VBUS Test.");
      return false;
    }

    int adcValueVBUS = analogRead(ADC_VBUS_PIN);
    float adcVoltageVBUS = (adcValueVBUS * 3.3) / 1023 * 2;
    Serial.print("VBUS Voltage: ");
    Serial.println(adcVoltageVBUS);

    if (adcVoltageVBUS >= 4.0) {
      break; // VBUS检测通过，继续检测3V3
    }

    delay(1000);
  }

  // 检测3V3电压
  startMillis = millis();
  while (millis() - startMillis < 30000) {
    if (buttonReleased) {
      Serial.println("Button Released! Exiting 3V3 Test.");
      return false;
    }

    int adcValue3V3 = analogRead(ADC_3V3_PIN);
    float adcVoltage3V3 = (adcValue3V3 * 3.3) / 1023 *2;
    Serial.print("3V3 Voltage: ");
    Serial.println(adcVoltage3V3);

    if (adcVoltage3V3 >= 3.2) {
      return true; // 3V3检测通过
    }

    delay(1000);
  }

  Serial.println("Voltage and 3V3 Test Failed!");
  return false;
}

////////////////////////////////////////////////////////////////////////////////////
// 充电电流检测
bool readChargingCurrentTest(float CHARGING_THRESHOLD) {
  const int MAX_ATTEMPTS = 30;
  const int REQUIRED_PASSES = 5;
  // const float CHARGING_THRESHOLD = -100.0;

  int successfulReadings = 0;
  float currentValue = 0.0;

  // 默认模式打开电池
  if (!sendAndParse(DFLTMODE_ON_CMD, currentValue, nullptr, 1000)) {
    Serial.println("DFLTMODE_ON_CMD command failed!");
    return false;
  }

  for (int i = 0; i < MAX_ATTEMPTS; i++) {
    if (buttonReleased) {
      Serial.println("Button Released! Exiting Charging Current Test.");
      return false;
    }

    if (sendAndParse(GET_CURRENT_CMD, currentValue, "mA", 1000)) {
      Serial.print("Parsed Current: ");
      Serial.println(currentValue);

      if (currentValue <= CHARGING_THRESHOLD) {
        successfulReadings++;
        if (successfulReadings >= REQUIRED_PASSES) {
          Serial.println("Charging Current Test Passed!");
          return true;
        }
      } else {
        successfulReadings = 0;
      }
    }

    delay(500);
  }

  Serial.println("Charging Current Test Failed!");
  return false;
}

////////////////////////////////////////////////////////////////////////////////////
// 合并UART测试
bool readUARTTest() {
  unsigned long startMillis = millis();
  bool initReadyReceived = false;
  bool testResultReceived = false;
  int Build_count=0;
  
  // 等待"Init Ready"信息
  while (millis() - startMillis < 60000) {
    if (buttonReleased) {
      Serial.println("Button Released! Exiting UART Test.");
      return false;
    }

    if (Serial1.available()) {
      String received = Serial1.readStringUntil('\n');
      Serial.print("UART Received: ");
      Serial.println(received);

      if (received.indexOf("Init Ready") != -1) {
        beep_xms(1000); // 蜂鸣器响一次
        
        // digitalWrite(XIAO_USB_SW, LOW); // 关闭USB开关
        initReadyReceived = true;
        break; // 收到"Init Ready"，继续等待测试结果
      }
    }
    delay(100);
  }

  if (!initReadyReceived) {
    Serial.println("Init Ready not received within timeout.");
    return false;
  }
  
  
  // 等待测试结果
  startMillis = millis();
  while (millis() - startMillis < 60000) {
    if (buttonReleased) {
      Serial.println("Button Released! Exiting UART Test.");
      return false;
    }

    if (Serial1.available()) {
      String received = Serial1.readStringUntil('\n');
      Serial.print("UART Received: ");
      Serial.println(received);

      if (received.indexOf("Test Passed") != -1) {
        testResultReceived = true;
        Serial.println("Test Results Received!");
      }
      else if (received.indexOf("Test_Results_Saved") != -1) {
        Serial.println("Test_Results_Saved!");
        flash_saved_state = true;
        return testResultReceived; // 测试结果通过
      }
      else if (received.indexOf("MIC_Test Start") != -1) {
        beep_xms(1000); // 蜂鸣器响一次
      }
      else if (received.indexOf("Wireless Failed") != -1) {
        
        Serial.println("Test Results Received:Wireless Failed");
        return false; // 测试结果
      }
    }

    delay(100);
  }

  Serial.println("UART Test Timed Out!");
  return false;
}

////////////////////////////////////////////////////////////////////////////////////
// 休眠电流检测
bool readBattryCurrentTest() {
  const float Battery_CURRENT_THRESHOLD = 300.0;
  const int MAX_ATTEMPTS = 30;
  const int REQUIRED_PASSES = 5;

  int successfulReadings = 0;
  float currentValue = 0.0;
  sendAndCheckResponse("sys off","system off",10000,2,NULL);
  digitalWrite(Signal_Control, LOW);
  digitalWrite(XIAO_USB_SW, LOW);
  delay(500);
  {
    for (int i = 0; i < MAX_ATTEMPTS; i++) {
      if (buttonReleased) {
        Serial.println("Button Released! Exiting Sleep Current Test.");
        return false;
      }

      if (sendAndParse(GET_SLEEP_CURRENT_CMD, currentValue, "uA", 1000)) {
        Serial.print("Parsed Sleep Current: ");
        Serial.println(currentValue);

        if (currentValue <= Battery_CURRENT_THRESHOLD ) {
          successfulReadings++;
          if (successfulReadings >= REQUIRED_PASSES) {
            digitalWrite(XIAO_USB_SW, HIGH);
            Serial.println("Sleep Current Test Passed!");
            Serial1.println("Sleep Current Test Passed!");
            sendAndParse(SINK_OFF_CMD, currentValue, nullptr, 1000);
            sendAndParse(SUPPLY_OFF_CMD, currentValue, nullptr, 1000);
            return true;
          }
        }
      }
      delay(200);
    }
  }

  Serial.println("Battery Current Test Failed!");
  sendAndParse(DFLTMODE_ON_CMD, currentValue, nullptr, 1000);
  
  return false;
}

//蓝牙扫描测试
bool bt_scan_test(){
  if(sendAndCheckResponse(bt_init,bt_init_pass,10000,3,NULL))//蓝牙初始化操作
  {
    delay(50);
    if(sendAndCheckResponse(bt_scan_on,bt_scan_pass,10000,3,bt_scan_off))
    {
      return true;
    }
  }
  return false;
}

//GPIO测试
// =========================================================================
//  GPIO 测试专用数据和函数
// =========================================================================
// 定义GPIO输出-输入对的结构
struct GpioPair {
  const char* setCmdPrefix; // 设置命令的前缀, e.g., "gpio set gpio1 4"
  const char* getCmdPrefix; // 获取命令的前缀, e.g., "gpio get gpio1 6"
  const char* description;  // 描述信息，用于打印日志
};
// 根据您的需求定义所有GPIO对
const GpioPair gpioPairs[] = {
  {"gpio set gpio1 4", "gpio get gpio1 6",  "Pair 1 (1_4 -> 1_6)"},
  {"gpio set gpio2 2", "gpio get gpio1 7",  "Pair 2 (2_2 -> 1_7)"},
  {"gpio set gpio2 4", "gpio get gpio1 10", "Pair 3 (2_4 -> 1_10)"},
  {"gpio set gpio2 1", "gpio get gpio1 11", "Pair 4 (2_1 -> 1_11)"}
};
const int numPairs = sizeof(gpioPairs) / sizeof(gpioPairs[0]);
// 辅助函数：设置指定的GPIO值
bool setGpio(const char* setCmdPrefix, int value) {
  char fullCommand[50];
  // 构造完整命令, e.g., "gpio set gpio1 4 1"
  sprintf(fullCommand, "%s %d", setCmdPrefix, value);
  
  // 假设设置成功会返回 "OK" 或类似确认信息
  // 如果您的设备返回的是命令本身，可以将 "OK" 改为 setCmdPrefix
  return sendAndCheckResponse(fullCommand, fullCommand, 500, 2, NULL);
}
// 辅助函数：获取并检查GPIO的值
bool checkGpioValue(const char* getCmdPrefix, int expectedValue) {
  char expectedResponse[2]; // 用于存放 "0" 或 "1"
  sprintf(expectedResponse, "%d", expectedValue);
  
  // 调用核心函数，命令是get前缀，期望在返回值里找到 "0" 或 "1"
  // 假设返回值格式为 "value: 1" 或类似，strstr可以找到 '1'
  return sendAndCheckResponse(getCmdPrefix, expectedResponse, 500, 2, NULL);
}
// 主测试函数
bool runGpioTest() {
  Serial.println("\n\n==================== GPIO Loopback Test Start ====================");
  bool allTestsPassed = true;
  // --- 步骤 1: 全部复位为 0 ---
  Serial.println("\n[Phase 1] Resetting all output GPIOs to 0...");
  for (int i = 0; i < numPairs; i++) {
    Serial.print("  Resetting ");
    Serial.print(gpioPairs[i].description);
    if (setGpio(gpioPairs[i].setCmdPrefix, 0)) {
      Serial.println(" -> OK");
    } else {
      Serial.println(" -> FAILED!");
      allTestsPassed = false;
    }
  }
  if (!allTestsPassed) {
    Serial.println("\nERROR: Failed to reset all pins. Aborting test.");
    Serial.println("==================== Test Finished (FAIL) ====================");
    return false;
  }
  // --- 步骤 2: "Walking Ones" 顺序测试 ---
  Serial.println("\n[Phase 2] Starting 'Walking Ones' sequential test...");
  
  // 外层循环：依次将每个输出置为 1
  for (int i = 0; i < numPairs; i++) {
    if (buttonReleased) {
      Serial.println("Button Released! Exiting VBUS Test.");
      return false;
    }
    Serial.print("\n--- Testing ");
    Serial.println(gpioPairs[i].description);
    
    // a) 将当前要测试的输出引脚设置为 1
    Serial.print("  Setting output HIGH...");
    if (!setGpio(gpioPairs[i].setCmdPrefix, 1)) {
        Serial.println(" -> FAILED!");
        allTestsPassed = false;
        continue; // 继续测试下一个引脚对
    } else {
        Serial.println(" -> OK");
    }
    // b) 延时一小段时间确保电平稳定
    delay(50);
    // c) 内层循环：检查所有输入引脚的状态
    Serial.println("  Checking all corresponding inputs:");
    for (int j = 0; j < numPairs; j++) {
      int expected = (i == j) ? 1 : 0; // 如果是当前测试的对，期望为1，否则为0
      Serial.print("    - Checking ");
      Serial.print(gpioPairs[j].description);
      Serial.print(" (expecting ");
      Serial.print(expected);
      Serial.print(")...");
      if (checkGpioValue(gpioPairs[j].getCmdPrefix, expected)) {
        Serial.println(" -> PASS");
      } else {
        Serial.println(" -> FAIL!");
        allTestsPassed = false;
      }
    }
    // d) 将当前测试的输出引脚复位为 0，为下一次循环做准备
    Serial.print("  Resetting output LOW...");
    if (!setGpio(gpioPairs[i].setCmdPrefix, 0)) {
        Serial.println(" -> FAILED!");
        allTestsPassed = false;
    } else {
        Serial.println(" -> OK");
    }
  }
  // --- 步骤 3: 报告最终结果 ---
  Serial.println("\n==================== Test Summary ====================");
  if (allTestsPassed) {
    Serial.println("Result: ALL GPIO LOOPBACK TESTS PASSED!");
  } else {
    Serial.println("Result: ONE OR MORE GPIO LOOPBACK TESTS FAILED!");
    return false;
  }
  Serial.println("======================================================");
  return true;
}

//麦克风录音测试
bool runMicTest() {
  Serial.println("\n\n==================== Microphone Test Start ====================");
  
  const char* command = "mic capture 0";
  int maxRetries = 3;
  unsigned long timeout = 10000; // 麦克风捕获超时时间
  int retryCount = 0;
  
  char responseBuffer[2048];
  
  while (retryCount < maxRetries) {
    if (buttonReleased) {
      Serial.println("Button Released! Exiting MIC Test.");
      return false;
    }
    Serial.print("--> Attempt ");
    Serial.print(retryCount + 1);
    Serial.print("/");
    Serial.println(maxRetries);
    // 1. 发送命令 (自动附加 \r\n)
    Serial.print("    Sending command to Serial1: ");
    Serial.println(command);
    Serial1.println(command);
    beep_xms(800);
    // 2. 等待并读取响应
    // 先清空串口接收缓冲区，防止读取到旧数据
    while (Serial1.available()) {
      Serial1.read();
    }
    
    unsigned long startTime = millis();
    bool attemptFinished = false; // 标记本次尝试是否已处理（无论成功失败）
    while (millis() - startTime < timeout && !attemptFinished) {
      if (buttonReleased) {
        Serial.println("Button Released! Exiting MIC Test.");
        return false;
      }
      if (Serial1.available()) {
        memset(responseBuffer, 0, sizeof(responseBuffer));
        size_t length = Serial1.readBytesUntil('\n', responseBuffer, sizeof(responseBuffer) - 1);
        responseBuffer[length] = '\0';
        Serial.print("    Received from Serial1: ");
        Serial.println(responseBuffer);
        // 3. 解析响应字符串 (新增了对Min值的解析)
        int maxValue = 0;
        int minValue = 0; // 新增变量来存储 Min 值
        int consecutiveValue = 0;
        // 修改 sscanf 来捕获 Min 值，而不是忽略它
        int itemsParsed = sscanf(responseBuffer, "audio data Max: %d Min: %d Max consecutive: %d", &maxValue, &minValue, &consecutiveValue);
        
        if (itemsParsed == 3) { // 成功解析出我们需要的所有三个值
          Serial.println("    Successfully parsed response.");
          Serial.print("      - Parsed Max value: "); Serial.println(maxValue);
          Serial.print("      - Parsed Min value: "); Serial.println(minValue); // 打印 Min 值
          Serial.print("      - Parsed Max consecutive value: "); Serial.println(consecutiveValue);
          // 4. 根据新的标准进行判断
          bool maxCriteriaMet = (maxValue > 400);
          bool minCriteriaMet = (minValue < -400); // 新增 Min 值判断条件
          bool consecutiveCriteriaMet = (consecutiveValue < 5);
          Serial.print("    Checking criteria: [(Max > 400) OR (Min < -400)] AND [Max consecutive < 5]");
          
          if ((maxCriteriaMet || minCriteriaMet) && consecutiveCriteriaMet) {
            Serial.println("\n--> Result: MIC TEST PASSED");
            Serial.println("==================== Test Finished (PASS) ====================");
            return true; // 测试通过，函数立即成功返回
          } else {
            // 新逻辑：数值不符合标准，不立即失败，而是结束本次尝试并准备重试
            Serial.println("\n--> This attempt FAILED (Values out of range). Will retry if possible.");
            attemptFinished = true; // 标记本次尝试结束
            // 注意：这里不再有 return false;
          }
        } else {
           // 解析失败，可能响应格式不正确。继续在超时时间内等待正确格式的行。
           Serial.println("    Failed to parse response. Waiting for more data or timeout...");
        }
      }
      // delay(20); // 在循环中，尤其是有buttonReleased检查时，最好不要用delay
    } // end of inner timeout while loop
    // 如果代码执行到这里，有两种可能：
    // 1. 超时了 (attemptFinished 仍然是 false)
    // 2. 收到了响应但数值不达标 (attemptFinished 是 true)
    
    retryCount++; // 无论哪种情况，都增加重试计数
    
    if (retryCount < maxRetries) {
        if (!attemptFinished) { // 仅在超时的情况下打印此消息
            Serial.println("--> Timeout waiting for a valid response. Retrying...");
        }
        delay(500); // 重试前等待
    }
  }
  
  // 所有重试都失败了
  Serial.println("--> Result: MIC TEST FAILED (Max retries reached)");
  Serial.println("==================== Test Finished (FAIL) ====================");
  return false;
}

//ADC采电池电压测试
bool ADC_Test() {
  Serial.println("\n\n==================== ADC Test Start ====================");
  
  // --- 步骤 1: 配置 GPIO ---
  Serial.println("  [Step 1] Configuring GPIO1_15 as output...");
  Serial1.println("gpio conf gpio1 15 o");
  Serial.println("  --> GPIO configured successfully.");
  // --- 步骤 2: 设置 GPIO 为高电平 ---
  Serial.println("  [Step 2] Setting GPIO1_15 to HIGH...");
  Serial1.println("gpio set gpio1 15 1");
  Serial.println("  --> GPIO set to HIGH successfully.");
  // 给ADC一些稳定时间
  delay(100); 
  // --- 步骤 3: 读取并验证 ADC 值 ---
  Serial.println("  [Step 3] Reading ADC value for Channel 7...");
  const char* command = "adc_read get";
  int maxRetries = 3;
  unsigned long timeout = 3000; // 响应较长，超时时间可以适当增加
  int retryCount = 0;
  
  char responseLine[128]; // 用于存储单行响应
  while (retryCount < maxRetries) {
    Serial.print("    --> Attempt ");
    Serial.print(retryCount + 1);
    Serial.print("/");
    Serial.println(maxRetries);
    // 发送 ADC 读取命令
    Serial1.println(command);
    unsigned long startTime = millis();
    bool foundChannel7 = false; // 状态标志：是否已找到 "channel 7:"
    // 在超时时间内，持续读取和解析行
    while (millis() - startTime < timeout) {
      if (Serial1.available()) {
        memset(responseLine, 0, sizeof(responseLine));
        size_t length = Serial1.readBytesUntil('\n', responseLine, sizeof(responseLine) - 1);
        responseLine[length] = '\0';
        // 为了处理可能存在的缩进，我们先去除行首的空白字符
        char* trimmedLine = responseLine;
        while (isspace((unsigned char)*trimmedLine)) {
            trimmedLine++;
        }
        
        // 如果还没找到 Channel 7，就寻找 "channel 7:" 这一行
        if (!foundChannel7) {
          if (strstr(trimmedLine, "channel 7:") != NULL) {
            foundChannel7 = true;
            Serial.println("      Found 'channel 7:'. Now looking for 'vol' value...");
            // 找到后，继续循环，等待下一行数据
            continue; 
          }
        } 
        // 如果已经找到了 Channel 7，就在接下来的行中寻找 "vol ="
        else {
          int vol_mV = 0;
          // 使用 sscanf 解析 "vol = %d mV" 格式
          if (sscanf(trimmedLine, "vol = %d mV", &vol_mV) == 1) {
            Serial.print("      Parsed 'vol' value: ");
            Serial.print(vol_mV);
            Serial.println(" mV");
            // 将毫伏(mV)转换为伏特(V)
            float adcVoltage = vol_mV*2.0 / 1000.0;
            Serial.print("      Converted to Volts: ");
            Serial.println(adcVoltage, 4);
            // 判断值是否在范围内
            if (adcVoltage >= 3.5 && adcVoltage <= 4.2) {
              Serial.println("      --> ADC value is within the expected range [3.5, 4.2].");
              Serial.println("==================== Test Finished (PASS) ====================");
              return true; // 成功！
            } else {
              Serial.println("      --> ADC value is OUT of range. Retrying...");
              goto next_adc_retry; // 值不对，直接跳到重试逻辑
            }
          }
        }
      }
    } // end of inner while (timeout loop)
    next_adc_retry:; // goto 跳转标签
    retryCount++;
    Serial.println("    --> Attempt failed (Timeout or value out of range).");
    if(retryCount < maxRetries) {
        delay(500);
    }
  }
  // 所有重试都失败了
  Serial.println("  --> FAILED to get a valid ADC reading for Channel 7 after all retries.");
  Serial.println("==================== Test Finished (FAIL) ====================");
  return false;
}

bool imu_test() {
  Serial.println("\n\n==================== IMU Test Start ====================");
  
  const char* command = "imu get";
  int maxRetries = 3;
  unsigned long timeout = 3000; // IMU 可能需要一些时间来采样和响应
  int retryCount = 0;
  
  char responseLine[256]; // 用于存储单行响应
  while (retryCount < maxRetries) {
    if (buttonReleased) {
      Serial.println("Button Released! Exiting MIC Test.");
      return false;
    }
    Serial.print("--> Attempt ");
    Serial.print(retryCount + 1);
    Serial.print("/");
    Serial.println(maxRetries);
    // 1. 发送命令
    Serial.print("    Sending command to Serial1: ");
    Serial.println(command);
    Serial1.println(command);
    // 2. 在超时时间内等待并解析响应
    unsigned long startTime = millis();
    while (millis() - startTime < timeout) {
      if (buttonReleased) {
        Serial.println("Button Released! Exiting MIC Test.");
        return false;
      }
      if (Serial1.available()) {
        memset(responseLine, 0, sizeof(responseLine));
        size_t length = Serial1.readBytesUntil('\n', responseLine, sizeof(responseLine) - 1);
        responseLine[length] = '\0';
        Serial.print("    Received: ");
        Serial.println(responseLine);
        // 3. 检查是否是加速度计数据行，并解析
        // 我们只关心 "accel data:" 开头的行
        if (strstr(responseLine, "accel data:") != NULL) {
          float accel_x, accel_y, accel_z;
          // 使用 sscanf 解析格式为 "accel data: %f, %f, %f" 的字符串
          int itemsParsed = sscanf(responseLine, "accel data: %f, %f, %f", &accel_x, &accel_y, &accel_z);
          if (itemsParsed == 3) {
            Serial.println("    Successfully parsed accel data.");
            Serial.print("      - Parsed Z-axis value: ");
            Serial.println(accel_z, 6); // 打印6位小数以提高精度
            // 4. 根据标准进行判断
            if (accel_z > 9) {
              Serial.println("    --> Z-axis value is > 9.5. Test PASSED.");
              Serial.println("==================== Test Finished (PASS) ====================");
              return true; // 测试通过，函数成功返回
            } else {
              Serial.println("    --> Z-axis value is NOT > 9. Retrying...");
              goto next_imu_retry; // 值不符合标准，直接跳到重试逻辑
            }
          }
        }
        // 如果不是 "accel data:" 行，就忽略它，继续等待
      }
    } // end of inner timeout while loop
    next_imu_retry:; // goto 跳转标签
    retryCount++;
    Serial.println("    --> Attempt failed (Timeout or value out of range).");
    if (retryCount < maxRetries) {
      delay(500);
    }
  }
  // 所有重试都失败了
  Serial.println("--> FAILED to get a valid IMU reading after all retries.");
  Serial.println("==================== Test Finished (FAIL) ====================");
  return false;
}

bool Return_Flash_Sava_result()
{
  unsigned long timeoutMillis = 10000;
  unsigned long startTime = millis(); // 获取当前时间作为开始时间
  digitalWrite(Signal_Control, HIGH);
  delay(100);

  while (1) {
    // 1. 检查是否超时
    if (millis() - startTime >= timeoutMillis) {
      Serial.println("Timeout waiting for Over");
      return false; // 返回0表示超时或其他错误
    }

    // 2. 检查串口是否有数据
    if (Serial1.available()) {
      String command = Serial1.readStringUntil('\n');
      command.trim(); // 去除多余的空格
      Serial.println(command); // 调试时可以取消注释

      if (command == "Test_Results_Saved") {
        Serial.println("TEST PASS");
        digitalWrite(Signal_Control, LOW);
        return true; // 返回1表示成功接收到命令
      }
    }
    delay(100);
    // 可以在这里加入一个小的延时，避免CPU占用过高，但要注意不要影响超时判断的精度

  }
  
}

bool button_test() {
  Serial.println("\n\n==================== Button Test Start ====================");
  beep_xms(1000);
  // --- 配置常量 ---
  // !!! 重要: 请将此字符串修改为您设备复位后打印的实际信息 !!!
  const char* EXPECTED_RESET_MESSAGE = "Booting nRF Connect SDK"; 
  
  const char* PRESS_STRING = "usr button pressed";
  const char* RELEASE_STRING = "usr button released";
  const unsigned long TEST_TIMEOUT = 20000UL; // 20秒的交互超时时间
  // --- 状态变量 ---
  bool sawPress = false;    // 是否检测到按下
  bool sawRelease = false;  // 是否检测到松开 (必须在按下之后)
  char responseLine[256];
  // --- 打印操作指南 ---
  Serial.println("\n>>> Please perform the following actions within 20 seconds:");
  Serial.println("    1. Press and hold the main button.");
  Serial.println("    2. Release the main button.");
  Serial.println("    3. Press the RESET button on the device.");
  Serial.println(">>> Now listening for device output on Serial1...");
  unsigned long startTime = millis();
  // --- 主监听循环 ---
  while (millis() - startTime < TEST_TIMEOUT) {
    if (buttonReleased) {
      Serial.println("Button Released! Exiting MIC Test.");
      return false;
    }
    if (Serial1.available()) {
      memset(responseLine, 0, sizeof(responseLine));
      size_t length = Serial1.readBytesUntil('\n', responseLine, sizeof(responseLine) - 1);
      responseLine[length] = '\0';
      // 实时打印从设备收到的所有信息，方便调试
      Serial.print("    [Device Output]: ");
      Serial.println(responseLine);
      // --- 状态机逻辑 ---
      // 1. 检测 "button pressed"
      if (strstr(responseLine, PRESS_STRING) != NULL) {
        Serial.println("    -> Detected: Button Press. OK. Now release the button.");
        sawPress = true;
        sawRelease = false; // 如果用户多次按下，重置松开状态
        continue; // 继续监听
      }
      // 2. 检测 "button released" (必须在按下之后)
      if (sawPress && strstr(responseLine, RELEASE_STRING) != NULL) {
        Serial.println("    -> Detected: Button Release. OK. Now press the RESET button.");
        sawRelease = true;
        continue; // 继续监听
      }
      // 3. 检测复位信息 (必须在按下和松开都完成之后)
      if (sawPress && sawRelease && strstr(responseLine, EXPECTED_RESET_MESSAGE) != NULL) {
        Serial.println("    -> Detected: Correct reset message after button sequence.");
        Serial.println("--> Result: BUTTON TEST PASSED");
        Serial.println("==================== Test Finished (PASS) ====================");
        return true; // 所有步骤正确完成，测试通过！
      }
    }
  } // end of while loop
  // 如果循环结束还没有返回 true，说明超时了
  Serial.println("\n--> TEST TIMEOUT! The required sequence was not completed in time.");
  Serial.println("    Last state: ");
  Serial.print("      - Saw Press: "); Serial.println(sawPress ? "Yes" : "No");
  Serial.print("      - Saw Release: "); Serial.println(sawRelease ? "Yes" : "No");
  Serial.println("--> Result: BUTTON TEST FAILED");
  Serial.println("==================== Test Finished (FAIL) ====================");
  return false;
}

////////////////////////////////////////////////////////////////////////////////////
// 执行测试
bool performTest() {
  switch (testState) {
    //case 0空
    case 1:
      // 合并VBUS和3V3检测
      digitalWrite(XIAO_USB_SW, HIGH);  //打开线路开关检测电压和UART通信
      if (readVoltageAnd3V3Test()) {
        ledState[0] = 2; // 测试通过，LED常亮
        testState++;
        Serial.println("Voltage and 3V3 Test Passed!");
      } else {
        ledState[0] = 1; // 测试失败，LED闪烁
        Serial.println("Voltage and 3V3 Test Failed!");
        testState = 0;
        return false;
      }
      break;

    case 2:
      // 充电测试
      if (readChargingCurrentTest(-100)) {
        ledState[1] = 2; // 测试通过，LED常亮
        testState++;

        Serial.println("Charging Current Test Passed!");
      } else {
        ledState[1] = 1; // 测试失败，LED闪烁
        Serial.println("Charging Current Test Failed!");
        testState = 0;
        return false;
      }
      break;

    case 3:
      // 蓝牙测试
      if(bt_scan_test()){
        ledState[2] = 2; // 测试通过，LED常亮
        Serial.println("ble Test Passed!");
      }else{
        ledState[2] = 1; // 测试失败，LED闪烁
        Serial.println("bt Test Failed!");
        testState = 0;
        return false;
      }
      if(runGpioTest()){
        ledState[2] = 2; // 测试通过，LED常亮
        Serial.println("GPIO Test Passed!");
      }else{
        ledState[2] = 1; // 测试失败，LED闪烁
        Serial.println("GPIO Test Failed!");
        testState = 0;
        return false;
      }
      // if(runMicTest()){
      //   ledState[2] = 2; // 测试通过，LED常亮
      //   Serial.println("MIC Test Passed!");
      // }else{
      //   ledState[2] = 1; // 测试失败，LED闪烁
      //   Serial.println("MIC Test Failed!");
      //   testState = 0;
      //   return false;
      // }
      if(ADC_Test()){
        ledState[2] = 2; // 测试通过，LED常亮
        testState++;
        Serial.println("ADC Test Passed!");
      }else{
        ledState[2] = 1; // 测试失败，LED闪烁
        Serial.println("ADC Test Failed!");
        testState = 0;
        return false;
      }
      // if(imu_test()){
      //   ledState[2] = 2; // 测试通过，LED常亮
      //   testState++;
      //   Serial.println("IMU Test Passed!");
      // }else{
      //   ledState[2] = 1; // 测试失败，LED闪烁
      //   Serial.println("IMU Test Failed!");
      //   testState = 0;
      //   return false;
      // }
      break;

    case 4:
      // 按键检测
      if (button_test()) {
        ledState[3] = 2; // 测试通过，LED常亮
        testState=0;
        beep_xms(2000);
        Serial.println("Button Test Passed!");
      } else {
        ledState[3] = 1; // 测试失败，LED闪烁
        Serial.println("Button Test Failed!");
        testState = 0;
        return false;
      }
      break;

    case 5:
    // flash写入测试结果检测
    if(flash_saved_state)
    {
      beep_xms(1000);
      for (int i = 0; i < 4; i++) {
        ledState[i] = 3;  //保存成功后闪烁指示灯
      }
      testState = 0;
      return true;
    }else{
      testState = 0;
      return false;
    }    
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////////
// 主循环
void loop() {
  bool currentButtonState = digitalRead(BUTTON_PIN) == HIGH;

  if (currentButtonState && !lastButtonState) {
    buttonPressed = true;
    buttonReleased = false;
    testState = 1;
    digitalWrite(XIAO_USB_SW, HIGH); // 打开USB开关
    Serial.println("Button Pressed! Start Voltage and 3V3 Test");
  }

  if (!currentButtonState && lastButtonState) {
    buttonReleased = true;
    resetSystem();
    Serial.println("Button Released! Test Reset");
  }

  lastButtonState = currentButtonState;

  if (buttonPressed) {
    if (!performTest()) {
      Serial.println("Test Failed! Indicating with blinking LED.");
    }

    
  }

  manageLEDs();
}