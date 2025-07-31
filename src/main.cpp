// ============= 库文件包含 =============
#include <WiFi.h>
#include <WebServer.h>
#include "Ultrasonic.h"
#include <ArduinoJson.h> // 强烈推荐用于JSON处理

// ============= 配置区域 (请根据您的环境修改) =============
const char* WIFI_SSID = "X.factory2.4G";      // 修改为您的WiFi名称
const char* WIFI_PASSWORD = "make0314";  // 修改为您的WiFi密码

const int ULTRASONIC_PIN = 44; // 超声波传感器连接的GPIO引脚 (Trig/Echo 单线模式)
const float DEFAULT_BUCKET_HEIGHT_CM = 100.0; // 水桶/容器的默认总高度 (厘米)
const float DEFAULT_THRESHOLD_PERCENT = 50.0; // 默认警戒水位百分比

// Web服务器
WebServer server(80);

// 超声波传感器
Ultrasonic ultrasonic(ULTRASONIC_PIN);

// 存储配置值 (可被网页修改)
float bucketHeight = DEFAULT_BUCKET_HEIGHT_CM;
float thresholdPercent = DEFAULT_THRESHOLD_PERCENT;

// 存储最近的超声波测量值和时间
float currentDistanceCm = -1.0; // -1 表示无效
unsigned long lastMeasurementTime = 0;
const unsigned long MEASUREMENT_INTERVAL_MS = 250; // 每250ms尝试测量一次

// ============= HTML 网页内容 (内嵌) =============
String generateHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="zh">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 水位监测</title>
  <!-- Chart.js 用于绘制图表 -->
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <!-- Chart.js 注解插件，用于绘制警戒线 -->
  <script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-annotation@1.4.0"></script>
  <style>
    * {
      box-sizing: border-box;
    }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      margin: 0;
      padding: 0;
      background-color: #f8f9fa;
      color: #333;
    }
    header {
      padding: 20px;
      background-color: #007bff;
      color: white;
      text-align: center;
    }
    .container {
      padding: 20px;
      max-width: 100%;
      width: 100%;
      margin: 0 auto;
    }
    .chart-container {
      position: relative;
      height: 400px;
      width: 100%;
      background: #fff;
      padding: 15px;
      border-radius: 8px;
      box-shadow: 0 2px 8px rgba(0, 0, 0, 0.05);
      margin-bottom: 20px;
    }
    .form-group,
    .info {
      background-color: #ffffff;
      padding: 20px;
      border-radius: 8px;
      margin-top: 20px;
      box-shadow: 0 2px 8px rgba(0, 0, 0, 0.05);
      text-align: center;
    }
    .form-group label {
      margin-right: 10px;
      font-weight: 500;
    }
    input[type="number"] {
      padding: 8px;
      width: 120px;
      margin: 5px;
      border: 1px solid #ccc;
      border-radius: 4px;
      font-size: 16px;
    }
    button {
      padding: 10px 20px;
      background-color: #28a745;
      border: none;
      border-radius: 4px;
      color: white;
      font-weight: bold;
      cursor: pointer;
      font-size: 16px;
      margin-top: 10px;
    }
    button:hover {
      background-color: #218838;
    }
    .distance-value {
      font-size: 28px;
      font-weight: bold;
      color: #007bff;
    }
    .unit {
      font-size: 18px;
      color: #666;
    }
    .config-link {
      position: absolute;
      top: 20px;
      right: 20px;
      color: white;
      text-decoration: none;
      font-weight: bold;
    }
    .config-link:hover {
      text-decoration: underline;
    }
    .lang-select {
      position: absolute;
      top: 60px;
      right: 20px;
    }
    select {
      padding: 5px 10px;
      font-size: 14px;
    }
    @keyframes flashRed {
      0% { background-color: #f8f9fa; }
      50% { background-color: #ffcccc; }
      100% { background-color: #f8f9fa; }
    }
    body.alert {
      animation: flashRed 1s infinite;
    }
  </style>
</head>
<body>
  <!-- 语言选择 -->
  <div class="lang-select">
    <select id="langSelect">
      <option value="zh">中文</option>
      <option value="en">English</option>
    </select>
  </div>
  <!-- WiFi配置链接 -->
  <a href="/wifi-config" class="config-link">WiFi配置</a>
  <!-- 页面标题 -->
  <h1>ESP32-S3 超声波水位监测</h1>
  <p>实时监测水位数据:</p>

  <!-- 配置输入区 -->
  <div class="form-group">
    <label>水桶高度 (cm): <input type="number" id="bucketHeightInput" value=")rawliteral" + String(DEFAULT_BUCKET_HEIGHT_CM) + R"rawliteral(" min="1" step="0.1"></label>
    <label>警戒水位 (%)：<input type="number" id="thresholdInput" value=")rawliteral" + String(DEFAULT_THRESHOLD_PERCENT) + R"rawliteral(" min="0" max="100" step="0.1"></label>
    <button id="applyBtn">应用设置</button>
  </div>

  <!-- 图表显示区 -->
  <div class="chart-container">
    <canvas id="distanceChart"></canvas>
  </div>

  <!-- 实时数据显示区 -->
  <div class="info">
    <p><span id="labelRaw">当前超声波距离:</span> <span id="rawDistance" class="distance-value">--</span> <span class="unit">cm</span></p>
    <p><span id="labelCurrent">当前水位:</span> <span id="currentDistance" class="distance-value">--</span> <span class="unit">%</span></p>
    <p><span id="labelUpdate">最后更新:</span> <span id="lastUpdate">--</span></p>
  </div>

  <!-- JavaScript 逻辑 -->
  <script>
    // 多语言支持
    const LANG = {
      zh: {
        title: "ESP32-S3 水位监测",
        description: "实时显示水位信息:",
        current: "当前水位:",
        raw: "当前超声波距离:",
        last: "最后更新:",
        confirm: "应用设置",
        wifi: "WiFi配置"
      },
      en: {
        title: "ESP32-S3 Water Level Monitor",
        description: "Live distance data from the ultrasonic sensor:",
        current: "Current Level:",
        raw: "Raw Distance:",
        last: "Last Update:",
        confirm: "Apply",
        wifi: "WiFi Config"
      }
    };

    // 设置当前语言
    function setLanguage(lang) {
      document.querySelector("h1").textContent = LANG[lang].title;
      document.querySelector("p").textContent = LANG[lang].description;
      document.querySelector(".config-link").textContent = LANG[lang].wifi;
      document.getElementById("applyBtn").textContent = LANG[lang].confirm;
      document.getElementById("labelCurrent").textContent = LANG[lang].current;
      document.getElementById("labelRaw").textContent = LANG[lang].raw;
      document.getElementById("labelUpdate").textContent = LANG[lang].last;
    }

    // 监听语言选择变化
    document.getElementById("langSelect").addEventListener("change", e => setLanguage(e.target.value));
    // 初始化语言
    setLanguage("zh");

    // 从服务器获取当前配置
    function fetchConfig() {
      fetch('/api/config')
        .then(response => response.json())
        .then(config => {
          // 更新全局变量和输入框
          bucketHeight = config.bucketHeight;
          thresholdPercent = config.threshold;
          document.getElementById('bucketHeightInput').value = bucketHeight;
          document.getElementById('thresholdInput').value = thresholdPercent;
          // 更新图表警戒线
          updateChartAlertLine();
        })
        .catch(error => {
          console.error("获取配置失败:", error);
          // 使用默认值
          bucketHeight = )rawliteral" + String(DEFAULT_BUCKET_HEIGHT_CM) + R"rawliteral(;
          thresholdPercent = )rawliteral" + String(DEFAULT_THRESHOLD_PERCENT) + R"rawliteral(;
          updateChartAlertLine();
        });
    }

    // 全局变量存储配置
    let bucketHeight = )rawliteral" + String(DEFAULT_BUCKET_HEIGHT_CM) + R"rawliteral(;
    let thresholdPercent = )rawliteral" + String(DEFAULT_THRESHOLD_PERCENT) + R"rawliteral(;

    // 应用按钮点击事件
    document.getElementById("applyBtn").addEventListener("click", () => {
      const newHeight = parseFloat(document.getElementById("bucketHeightInput").value);
      const newThreshold = parseFloat(document.getElementById("thresholdInput").value);

      // 输入验证
      if (isNaN(newHeight) || newHeight <= 0) {
        alert("请输入有效的大于0的水桶高度！");
        return;
      }
      if (isNaN(newThreshold) || newThreshold < 0 || newThreshold > 100) {
        alert("警戒水位必须在0-100之间！");
        return;
      }

      // 创建配置对象
      const config = {
        bucketHeight: newHeight,
        threshold: newThreshold
      };

      // 发送POST请求保存配置
      fetch('/api/saveConfig', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(config)
      })
      .then(response => {
        if (!response.ok) {
          throw new Error('保存配置失败: ' + response.status);
        }
        return response.text();
      })
      .then(message => {
        console.log("配置保存成功:", message);
        // 更新本地变量
        bucketHeight = newHeight;
        thresholdPercent = newThreshold;
        // 更新图表
        updateChartAlertLine();
        alert("配置已保存！");
      })
      .catch(error => {
        console.error("保存配置时出错:", error);
        alert("保存配置失败: " + error.message);
      });
    });

    // 更新图表中的警戒线
    function updateChartAlertLine() {
      if (typeof chart !== 'undefined' && chart) {
        chart.options.plugins.annotation.annotations.alertLine.yMin = thresholdPercent;
        chart.options.plugins.annotation.annotations.alertLine.yMax = thresholdPercent;
        chart.update();
      }
    }

    // 初始化图表
    const ctx = document.getElementById('distanceChart').getContext('2d');
    const chart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [{
          label: '水位 (%)',
          data: [],
          borderColor: 'rgb(54, 162, 235)',
          backgroundColor: 'rgba(54, 162, 235, 0.1)',
          tension: 0.1,
          fill: true
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
          y: {
            min: 0,
            max: 100,
            title: {
              display: true,
              text: '水位 (%)'
            }
          },
          x: {
            title: {
              display: true,
              text: '时间'
            }
          }
        },
        plugins: {
          annotation: {
            annotations: {
              alertLine: {
                type: 'line',
                yMin: thresholdPercent,
                yMax: thresholdPercent,
                borderColor: 'red',
                borderWidth: 2,
                borderDash: [6, 6],
                label: {
                  enabled: true,
                  content: '警戒线',
                  position: 'start'
                }
              }
            }
          }
        }
      }
    });

    // 更新图表和数据显示
    function updateChartDisplay(distanceCm) {
      // 计算水位百分比
      const waterLevelPercent = Math.max(0, Math.min(100, ((bucketHeight - distanceCm) / bucketHeight) * 100));
      const now = new Date();
      const timeStr = now.toLocaleTimeString();

      // 更新图表数据
      chart.data.labels.push(timeStr);
      chart.data.datasets[0].data.push(waterLevelPercent);

      // 限制数据点数量 (最多20个)
      if (chart.data.labels.length > 20) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
      }

      // 更新警戒线 (如果配置改变)
      chart.options.plugins.annotation.annotations.alertLine.yMin = thresholdPercent;
      chart.options.plugins.annotation.annotations.alertLine.yMax = thresholdPercent;

      // 刷新图表
      chart.update();

      // 更新文本显示
      document.getElementById("currentDistance").textContent = waterLevelPercent.toFixed(1);
      document.getElementById("rawDistance").textContent = distanceCm.toFixed(1);
      document.getElementById("lastUpdate").textContent = timeStr;

      // 警戒状态闪烁
      if (waterLevelPercent > thresholdPercent) {
        document.body.classList.add('alert');
      } else {
        document.body.classList.remove('alert');
      }
    }

    // 获取最新距离数据
    function fetchDistance() {
      fetch('/api/distance')
        .then(response => {
          if (!response.ok) {
            throw new Error('HTTP error! status: ' + response.status);
          }
          return response.json();
        })
        .then(data => {
          if (data.distance >= 0) {
            updateChartDisplay(data.distance);
          } else {
            console.warn("收到无效距离数据:", data.distance);
            // 可以在这里处理无效数据，例如显示错误信息
          }
        })
        .catch(error => {
          console.error("获取距离数据失败:", error);
          // 可以更新UI显示连接错误
          document.getElementById("rawDistance").textContent = "错误";
          document.getElementById("currentDistance").textContent = "错误";
          document.getElementById("lastUpdate").textContent = "连接失败";
        });
    }

    // ============= 页面初始化 =============
    // 首次获取配置
    fetchConfig();
    // 首次获取数据
    fetchDistance();
    // 每5秒自动刷新数据
    setInterval(fetchDistance, 5000);
  </script>
</body>
</html>
)rawliteral";

  return html;
}

// ============= Web 服务器处理函数 =============
void handleRoot() {
  server.send(200, "text/html", generateHTML());
}

void handleConfig() {
  StaticJsonDocument<100> doc;
  doc["bucketHeight"] = bucketHeight;
  doc["threshold"] = thresholdPercent;
  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}

float getCurrentDistance() {
  unsigned long currentTime = millis();
  // 检查是否需要进行新的测量
  if (currentTime - lastMeasurementTime >= MEASUREMENT_INTERVAL_MS) {
    long rangeInCm = ultrasonic.MeasureInCentimeters();
    // 检查测量结果是否在有效范围内 (0-400cm)
    if (rangeInCm >= 0 && rangeInCm <= 400) {
      currentDistanceCm = (float)rangeInCm;
      Serial.println("超声波测量: " + String(currentDistanceCm) + " cm");
    } else {
      Serial.println("超声波测量无效: " + String(rangeInCm) + " cm");
      // 保持上一次有效值或标记为无效 (-1.0)
      // currentDistanceCm = -1.0; // 或者不更新，保持上一次有效值
    }
    lastMeasurementTime = currentTime;
  }
  return currentDistanceCm;
}


void handleDistance() {
  float distance = getCurrentDistance();
  // 使用 ArduinoJson 创建 JSON 响应 (更健壮)
  StaticJsonDocument<100> doc;
  doc["distance"] = distance;
  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}


void handleSaveConfig() {
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      Serial.println("JSON解析失败: " + String(error.c_str()));
      server.send(400, "text/plain", "Bad Request: Invalid JSON");
      return;
    }

    // 提取并验证数据
    if (doc.containsKey("bucketHeight")) {
      float h = doc["bucketHeight"];
      if (h > 0) {
        bucketHeight = h;
        Serial.println("水桶高度更新为: " + String(bucketHeight) + " cm");
      } else {
        Serial.println("无效的水桶高度: " + String(h));
      }
    }

    if (doc.containsKey("threshold")) {
      float t = doc["threshold"];
      if (t >= 0 && t <= 100) {
        thresholdPercent = t;
        Serial.println("警戒水位更新为: " + String(thresholdPercent) + " %");
      } else {
        Serial.println("无效的警戒水位: " + String(t));
      }
    }

    server.send(200, "text/plain", "配置已保存");
  } else {
    server.send(405, "text/plain", "方法不允许");
  }
}

void handleWiFiConfig() {
  String html = "<!DOCTYPE html><html><head><title>WiFi 配置</title>";
  html += "<style>body {font-family: Arial, sans-serif; margin: 40px;}</style></head><body>";
  html += "<h1>ESP32-S3 WiFi 配置</h1>";
  html += "<p><strong>设备名称:</strong> ESP32-WaterLevelMonitor</p>";
  html += "<p><strong>IP 地址:</strong> " + WiFi.localIP().toString() + "</p>";
  html += "<p><strong>WiFi 名称 (SSID):</strong> " + String(WIFI_SSID) + "</p>";
  html += "<p><strong>信号强度:</strong> " + String(WiFi.RSSI()) + " dBm</p>";
  html += "<p><a href='/'>返回水位监测主页</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// 获取当前超声波距离（带缓存，避免过于频繁测量）

// ============= Setup 函数 =============
void setup() {
  // 初始化串口通信
  Serial.begin(115200);
  Serial.println("\n\n=== ESP32-S3 超声波水位监测器 启动 ===");

  // 连接到 WiFi 网络
  Serial.print("正在连接到 WiFi 网络: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int connectTimeout = 0;
  const int maxConnectAttempts = 60; // 最多等待30秒 (60 * 500ms)

  while (WiFi.status() != WL_CONNECTED && connectTimeout < maxConnectAttempts) {
    delay(500);
    Serial.print(".");
    connectTimeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi 连接成功！");
    Serial.print("🌐 设备IP地址: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi 连接失败！请检查SSID和密码。");
    // 即使连接失败，也继续启动Web服务器（可用于AP模式或后续重连）
  }

  // 设置 Web 服务器路由
  server.on("/", HTTP_GET, handleRoot); // 主页面
  server.on("/api/distance", HTTP_GET, handleDistance); // 获取距离数据API
  server.on("/api/config", HTTP_GET, handleConfig); // 获取当前配置API
  server.on("/api/saveConfig", HTTP_POST, handleSaveConfig); // 保存配置API
  server.on("/wifi-config", HTTP_GET, handleWiFiConfig); // WiFi信息页面

  // 启动 Web 服务器
  server.begin();
  Serial.println("🚀 内置Web服务器已启动 (端口 80)");

  // 初始化超声波传感器 (通常在库构造函数中完成)
  // ultrasonic 对象已在全局声明

  Serial.println("✅ 系统初始化完成。请在浏览器中访问设备IP地址。");
}

// ============= Loop 函数 =============
void loop() {
  // 处理所有传入的客户端请求
  server.handleClient();

  Serial.println("running");

  // 主循环中可以执行其他后台任务
  // 例如：定期检查WiFi连接状态并尝试重连
  // 但超声波测量已在 getCurrentDistance() 中由API调用触发，这里不需要额外循环

  // 添加一个小延迟，避免过度占用CPU





   if (WiFi.status() == WL_CONNECTED) {
      Serial.print("当前IP地址: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("未连接到WiFi网络。");
    }


    
  delay(10);
}