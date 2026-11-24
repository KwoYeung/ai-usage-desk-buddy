# Passport DSH 状态

这个 Host 插件把 DSH 的 agent 工作状态写成本机状态文件，供 AI Passport 的桌面桥接读取。

- 只写 `available`、`working`、`completion_seq`、`turn`、`updated`；
- 不读取提示词、回答、工具参数或登录凭据；
- 不创建网页，也没有设置页；
- 状态文件：`~/.dsh/passport-state.json`（0600）；
- 每 10 秒写一次心跳；插件卸载/DSH 退出时写 `available: false`；
- 只有顶层的 turn 会计入完成，子 agent 不会重复提醒；
- 取消的 turn 不递增完成序号。

如果没有安装或运行这个插件，Passport 不会显示 DSH 的工作中/完成提醒；不会伪造状态。
