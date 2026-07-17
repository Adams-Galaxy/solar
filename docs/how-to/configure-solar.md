# Configure Solar

Run Zephyr `menuconfig` for the application build and open the **Solar firmware
orchestration** menu:

```sh
west build -b your_board path/to/application
west build -t menuconfig
```

Enable only the subsystems the application owns. Set catalog and buffer maxima
from expected declarations and bursts, then use typed Blueprint configuration
for per-declaration policy. Regenerate the build after changing Kconfig because
feature inclusion changes the effective System type.
