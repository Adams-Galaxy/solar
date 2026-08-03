# Configure Solar

Run Zephyr `menuconfig` for the application build and open the **Solar firmware
orchestration** menu:

```sh
west build -b your_board path/to/application
west build -t menuconfig
```

Enable only the platform integrations the application needs. Set buffer maxima
from expected declarations and bursts, then use module type parameters and the
application composition for per-module policy. Regenerate after changing
Kconfig because limits affect the compiled runtime.
