import paho.mqtt.client as mqtt
import time

# =========================================================================
# MQTT Broker 设置 (需与 ESP32 和 网页端保持一致)
# =========================================================================
MQTT_BROKER = "broker.emqx.io"
MQTT_PORT = 1883

# 主题配置
TOPIC_STATE = "myhome/esp32/light/state"
TOPIC_CMD = "myhome/esp32/light/cmd"

def on_connect(client, userdata, flags, rc):
    """
    当客户端连接到Broker时触发的回调函数
    """
    if rc == 0:
        print("✅ 成功连接到 MQTT Broker!")
        # 订阅状态主题，监听灯的开关状态变化
        client.subscribe(TOPIC_STATE)
        print(f"📡 已订阅主题: {TOPIC_STATE}")
    else:
        print(f"❌ 连接失败，返回码: {rc}")

def on_message(client, userdata, msg):
    """
    当收到订阅主题的消息时触发的回调函数
    """
    payload = msg.payload.decode('utf-8')
    print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] 收到消息 | 主题: {msg.topic} | 内容: {payload}")
    
    # 这里可以添加您的业务逻辑，例如：
    # 1. 将状态记录到本地数据库或文本文件
    # 2. 触发其他联动设备
    if payload == "ON":
        print(">> 💡 记录状态: 硬件设备灯已打开")
    elif payload == "OFF":
        print(">> 🌙 记录状态: 硬件设备灯已关闭")

# 创建MQTT客户端实例
client = mqtt.Client()

# 设置回调函数
client.on_connect = on_connect
client.on_message = on_message

print("="*50)
print("启动 Python MQTT 后端服务...")
print("="*50)
print(f"正在连接到 {MQTT_BROKER}...")

try:
    # 连接到 MQTT Broker
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    
    # 启动网络循环（阻塞模式，持续监听）
    client.loop_forever()
    
except KeyboardInterrupt:
    print("\n🛑 检测到中断信号，服务已停止")
    client.disconnect()
except Exception as e:
    print(f"❌ 发生错误: {e}")

# 提示: 如果需要运行此脚本，请先安装 paho-mqtt 库
# pip install paho-mqtt
