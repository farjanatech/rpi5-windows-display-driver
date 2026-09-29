# Long-run hang recorder

This tester is self-contained. It does not write its soak logs into ProgramData
and it does not place the recovery ZIP on the Desktop.

After extracting the tester, all evidence is kept under:

`<tester folder>\Logs\`

Typical layout:

```text
Rpi5Display-Soak-Tester\
  Start-Soak-Watch.cmd
  Recover-Soak-Watch.cmd
  Soak-Watch.ps1
  Logs\
    <session-folder>\
      metrics.csv
      driver.etl
      ...
    Rpi5Display-Soak-Recovery-YYYYMMDD-HHMMSS.zip
```

## Test procedure

1. Extract the tester to a normal writable folder.
2. Boot Windows and wait until the desktop is behaving normally.
3. Run `Start-Soak-Watch.cmd`.
4. Keep that window running while using the Pi normally and during idle time.
5. Wait until the long-run lag/hang reproduces.
6. If Windows becomes too unresponsive to collect anything, reboot only as necessary.
7. After reboot, return to the **same extracted tester folder**.
8. Run `Recover-Soak-Watch.cmd`.
9. Open the local `Logs` subfolder.
10. Upload `Rpi5Display-Soak-Recovery-*.zip`.

The recorder writes one metrics row every 10 seconds and closes the CSV after
each row, so the most recent completed rows can survive a hard reset. It also
keeps a 64 MB circular ETW trace for the Rpi5Display TraceLogging provider with
a one-second flush interval.

Metrics include total CPU, DPC and interrupt time, processor queue, memory/pool
use, disk latency/queue, DWM/System process resource usage, processor clock/load,
display device status, driver version and ProblemCode.

The recorder does not install or change the display driver, firmware, power plan
or boot configuration.


## 0.1.18 Present-phase capture

The 0.1.18 diagnostic driver restores the normal hardware-VSync configuration
and emits trace-only Present phase events into the same circular ETW session.

Phase values:

- 1: PresentDisplayOnly entered the driver
- 2: adapter mutex acquired
- 3: shadow-buffer work completed
- 4: framebuffer flush completed successfully
- 5: Present exited with an error

Each event carries a PresentId so the last incomplete call can be identified.
The driver initializes QPC timing independently of VSync, so elapsed microseconds
remain valid even in future A/B builds.
