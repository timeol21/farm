# Framework

## 构建 Paho C++ 和 Paho C 库：

```bash
git clone https://github.com/eclipse/paho.mqtt.cpp
cd paho.mqtt.cppgit co v1.6.0

git submodule update --init

cmake -Bbuild -H. -DPAHO_WITH_MQTT_C=ON -DPAHO_BUILD_EXAMPLES=ON
sudo cmake --build build/ --target install
```



#### 完成后库会安装到 `/usr/local/` 目录下:

###### 头文件 (.h/.hpp): `/usr/local/include/`

######  库文件 (.so/.a): `/usr/local/lib/`



#### 在cmake中链接库:

```cmake
target_link_libraries(${PROJECT_NAME} paho-mqttpp3 paho-mqtt3a paho-mqtt3c)
```

#json
sudo apt install -y nlohmann-json3-dev