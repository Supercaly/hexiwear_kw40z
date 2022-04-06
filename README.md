 # Hexiwear library for KW40Z device
 
 ***This library is a port on mbed-os 6 of the [official KW40Z library](https://os.mbed.com/teams/Hexiwear/code/Hexi_KW40Z/) made by the HEXIWEAR team.***
 
 Library for the Hexiwear used to comunicate with the on-board KW40Z device and use his features: 
 
 - Bluetooth Low Energy
 - Touch buttons
 
## Getting started

To use this library with the new [mbed-cli 2](https://os.mbed.com/docs/mbed-os/v6.15/build-tools/mbed-cli-2.html) build system you need to follow this steps

1. manually create a `hexiwear_kw40z.lib` file containing the url of this git repository 
2. run `mbed-tools deploy` to download the required code
3. add the library to your main `CMakeLists.txt` file

```cmake
add_subdirectory(lib)
 
target_link_libraries(${APP_TARGET} 
    mbed-os 
    hexiwear_kw40z
)
```

## Simple Example

```c++
#include "hexiwear_kw40z/kw40z.h"
#include "mbed.h"

KW40Z kw40z_device(PTE24, PTE25);

void btn_fn()
{
    // Toggle on-off BLE
    kw40z_device.ToggleAdvertisementMode();
}

void passkey_fn()
{
    // Received BLE passkey
    printf("Passkey: %ld\n", kw40z_device.GetPassKey());
}

void tx_thread()
{
    while (true)
    {
        // Send dummy data
        kw40z_device.SendSetApplicationMode(GUI_CURRENT_APP_SENSOR_TAG);

        kw40z_device.SendAccel((int16_t)1.0, (int16_t)2.0, (int16_t)3.0);
        kw40z_device.SendGyro((int16_t)4.0, (int16_t)5.0, (int16_t)6.0);
        kw40z_device.SendMag((int16_t)7.0, (int16_t)8.0, (int16_t)9.0);
        kw40z_device.SendPressure(10);
        
        ThisThread::sleep_for(5s);
    }
}

int main()
{
    kw40z_device.attach_buttonLeft(&btn_fn);
    kw40z_device.attach_buttonRight(&btn_fn);
    kw40z_device.attach_passkey(&passkey_fn);

    Thread txThread;
    txThread.start(callback(&tx_thread));

    while (true)
    {
        ThisThread::sleep_for(500ms);
    }
    return 0;
}
```