# Long-run hang recorder

Use this when the desktop becomes too unresponsive to run `Collect-Logs.cmd` after the failure.

1. Boot normally and wait until the display is healthy.
2. Run `Start-Soak-Watch.cmd`.
3. Leave the window open and use/idle the Pi until the long-run lag reproduces.
4. If Windows is too unresponsive to collect anything, reboot only as necessary.
5. After reboot, run `Recover-Soak-Watch.cmd`.
6. Upload the generated `Rpi5Display-Soak-Recovery-*.zip` from the Desktop.

The recorder writes one metrics row every 10 seconds and closes the CSV file after each row, so the last completed samples survive a hard reset. It also keeps a 64 MB circular ETW trace for the Rpi5Display TraceLogging provider with a one-second flush interval.

Metrics include total CPU, DPC and interrupt time, processor queue, memory/pool use, disk latency/queue, DWM/System process resource usage, processor clock/load, display device status, driver version and ProblemCode.

The recorder does not change the driver, firmware, power plan or boot configuration.
