# Floral Device Architecture

## English

The framework has three planes with separate ownership and failure behavior.

```text
Host gateway
    +-- control.sock / FHC1
    +-- operate.sock / FDO1
    +-- video/<stream>.sock / FSV1 or FSV2
    `-- audio.sock / FSA1

Android container
    FloralDeviceService
        +-- FloralHalControlCenter
        +-- DisplayTopologyStateService
        +-- VideoSessionManager
        +-- AudioStreamSession
        `-- FloralDeviceOperationChannel

Separate Android components
    hardware_floral_display  -> HWC HAL
    hardware_floral_audio    -> primary audio HAL
    device_redroid           -> product integration
```

`control.sock` carries HAL and media configuration, capability queries, and
status. It does not carry touch or keyboard events.

`operate.sock` carries device operations such as touch, keyboard, mouse,
gestures, and explicit device actions. It does not change the persistent HAL
configuration model.

The HWC and audio real-time paths never parse either socket directly. Commands
are dispatched through bounded asynchronous queues or an atomic configuration
snapshot. A control disconnect must not block `presentDisplay()` or
AudioFlinger `out_write()`.

## 中文

Floral Device 分为三个平面，并分别处理生命周期与拥塞：

```text
宿主网关
    +-- control.sock / FHC1
    +-- operate.sock / FDO1
    +-- video/<stream>.sock / FSV1 或 FSV2
    `-- audio.sock / FSA1

Android 容器
    FloralDeviceService
        +-- FloralHalControlCenter
        +-- DisplayTopologyStateService
        +-- VideoSessionManager
        +-- AudioStreamSession
        `-- FloralDeviceOperationChannel

独立 Android 组件
    hardware_floral_display  -> HWC HAL
    hardware_floral_audio    -> primary 音频 HAL
    device_redroid           -> 产品集成
```

`control.sock` 负责 HAL 与媒体配置、能力查询和状态查询，不传输触摸或
键盘事件。

`operate.sock` 负责触摸、键盘、鼠标、手势和明确的设备动作，不负责修改
持久化 HAL 配置。

HWC 和音频实时路径不直接解析这两个 socket。命令必须通过有界异步队列或
原子配置快照分发，控制连接断开不得阻塞 `presentDisplay()` 或
AudioFlinger 的 `out_write()`。
