DefinitionBlock ("", "SSDT", 2, "AIR14", "BKEY", 0x00001000)
{
    External (_SB_.PC00.LPCB.PS2K, DeviceObj)
    External (_SB_.PC00.LPCB.H_EC, DeviceObj)
    External (_SB_.PC00.LPCB.H_EC.XQ8E, MethodObj)
    External (_SB_.PC00.LPCB.H_EC.XQ8F, MethodObj)

    Scope (\_SB.PC00.LPCB.H_EC)
    {
        Method (_Q8E, 0, NotSerialized)
        {
            If (_OSI ("Darwin"))
            {
                Notify (\_SB.PC00.LPCB.PS2K, 0x0406)
            }
            Else
            {
                XQ8E ()
            }
        }

        Method (_Q8F, 0, NotSerialized)
        {
            If (_OSI ("Darwin"))
            {
                Notify (\_SB.PC00.LPCB.PS2K, 0x0405)
            }
            Else
            {
                XQ8F ()
            }
        }
    }

    Scope (\_SB.PC00.LPCB.PS2K)
    {
        Name (RMCF, Package ()
        {
            "Keyboard", Package ()
            {
                "Custom ADB Map", Package ()
                {
                    "e052=72", // Insert key (e0 52) -> Help/Insert (0x72, kVK_Help)
                    "e037=69"  // PrtSc key (e0 37)  -> F13 (0x69)
                }
            }
        })
    }
}
