# Flexem MQTT 协议对接总结

结论: 本项目 MQTT 云端对接改为兼容繁易盒子报文。topic 以盒子号隔离, payload 为平铺 JSON, `time` 使用盒子本地 Unix 秒时间。

## 连接参数

- 测试 MQTT: `116.204.32.197:1883`
- 用户名: `admin`
- 密码: `llx@123456`
- QoS: `0`
- cleanSession: `true`
- 遗嘱: 暂无
- 生产盒子号不能随便测试; `300825090213` 是生产环境实际在用,不要使用。

## Topic

当前代码发布默认采用生产抓包格式: `flexem.{boxNo}.{endpoint}`。

为兼容 Excel 配置写法, 订阅同时接受 `flexem/{boxNo}/{endpoint}`。

| 功能         | 方向   | endpoint       | 说明                 |
| ---------- | ---- | -------------- | ------------------ |
| 变化上报       | 盒子到云 | `monitor`      | 点位变化平铺上报           |
| 立即获取全部状态   | 云到盒子 | `read`         | 云端下发要读的点位          |
| 立即获取全部状态响应 | 盒子到云 | `readdata`     | 返回点位当前值            |
| 故障/报警上报    | 盒子到云 | `pushalarm`    | 故障点位平铺上报           |
| 洗车命令下发     | 云到盒子 | `command`      | 启动/停止/复位           |
| 洗车命令响应     | 盒子到云 | `commandreply` | 成功才响应              |
| 重启命令       | 云到盒子 | `reboot`       | 当前代码只识别和记录,不直接执行重启 |

## Payload 规则

- 所有业务字段与 `boxNo`、`time` 同级,不放 `data` 对象。
- 一条 JSON 可以包含多个点位。
- `boxNo` 主要用于校对; topic 已经带盒子号。
- `time` 必须按盒子本地时间填 Unix 秒。

## 示例

变化上报:

```json
{"D142":3,"M1":0,"boxNo":"123456","time":1782121921}
```

立即读取:

```json
{"M187":null,"Y51":null,"D142":null,"M1":null,"Y50":null,"boxNo":"123456","Y74":null,"Y52":null,"time":null}
```

读取响应:

```json
{"D142":3,"M1":0,"M187":0,"Y50":0,"Y51":0,"Y52":0,"Y74":1,"boxNo":"123456","time":1782121980}
```

故障上报:

```json
{"X44":1,"boxNo":"123456","time":1782115670}
```

命令下发:

```json
{"Manual start":1,"boxNo":"123456","time":1774500653}
```

```json
{"stop":1,"boxNo":"123456","time":1774500653}
```

```json
{"reset":1,"boxNo":"123456","time":1774500653}
```

重启命令下发:

```json
{"reboot":1,"boxNo":"123456","time":1774500653}
```

命令成功响应:

```json
{"message":"success","time":1782119879}
```

失败且盒子在线时,按对方确认不返回应答。

## 当前代码映射

- `progress/post`、`change/post` -> `monitor`
- `daily/post`、`fault/post`、`event/post` -> `pushalarm`
- `info/post` -> `readdata`
- `start/post`、`stop/post`、`point/post` -> `commandreply`

## 补充

- 如果云端最终只接受斜杠 topic,把 `MqttClient::buildTopic` 的默认分隔符从 `.` 改成 `/` 即可
  （现在不用改代码：`config.json` 里 `mqtt.topicSeparator` 配 `"/"`）。

---

## 本项目实现状态（washcar-test, 架构重构后对齐）

> 链路：`MQTT 收包 → 下行队列(256) → DownlinkPump(50ms) → CommandParser → BusinessLayer → DataLayer(PLC)`
> 上行：`业务层 push → 上行队列(128) → UplinkPump(50ms) → MQTT 发出`

| 约定项 | 状态 | 实现位置 |
| --- | --- | --- |
| 发布 `flexem.{boxNo}.{endpoint}` | 已完成（分隔符可配 `mqtt.topicSeparator`） | `presentation/mqtt_client.cpp` `buildTopic` |
| 订阅兼容 `flexem/{boxNo}/{endpoint}` | 已完成（点号/斜杠两种都订阅，共 6 条） | `presentation/mqtt_client.cpp` `start` |
| `command` 订阅与下发 | 已完成 | `presentation/command_parser.cpp` `classifyCommand` → `business_layer.cpp` `dispatch` |
| `read` → `readdata` | 已完成（解析平铺点位 → 采样 → 回发） | `CommandParser::extractRequestedPoints` + `BusinessLayer::handleRead` + `WashCarServiceBase::readPoints` |
| `reboot` 只识别记录 | 已完成（默认只记录；`mqtt.rebootEnabled=true` 时优雅退出，由守护脚本拉起） | `BusinessLayer::dispatch` 的 `REBOOT` 分支 |
| `commandreply` 仅成功、`time` 整型 | 已完成 | `BusinessLayer::replySuccess` |
| `monitor` 变化上报 | 已完成 | `ReportScheduler` + `executeValueChangeTask` |
| `pushalarm` 故障上报 | 已完成 | `ReportScheduler` + `executeFaultReportTask` |
| `pushalarm` 每日物料上报 | 已完成 | `ReportScheduler` + `executeDailyReportTask` |
| 平铺 payload / `boxNo`、`time` 同级 | 已完成 | `addCommonFlexemFields` |
| MQTT 回调不阻塞（命令不再在网络线程里执行） | 已完成（本轮重构） | `presentation` 队列 + `DownlinkPump` |
| 断链后收敛为"最新一条" | 已完成（不是完整补发） | `common/buffer/uplink_buffer.h` |
| 未完成 | 失败回执（协议未定）、双格式去重、断链期间历史过程还原 | 见 `MQTT通信框架-设计与实现.md` §9 |

> 生产盒子号 `300825090213` 仍未用于任何测试。
