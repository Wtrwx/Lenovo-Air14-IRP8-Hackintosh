// Lenovo Air14 IRP8: identify the backlight without touching legacy PWM MMIO.
// WhateverXe owns the panel's native PWM path. UID 19 selects its profile.
DefinitionBlock ("", "SSDT", 2, "AIR14", "PNLF", 0x00000001)
{
    External (\_SB.PC00.GFX0, DeviceObj)

    If (_OSI ("Darwin"))
    {
        Scope (\_SB.PC00.GFX0)
        {
            Device (PNLF)
            {
                Name (_HID, EisaId ("APP0002"))
                Name (_CID, "backlight")
                Name (_UID, 19)
                Name (_STA, 0x0B)
            }
        }
    }
}
