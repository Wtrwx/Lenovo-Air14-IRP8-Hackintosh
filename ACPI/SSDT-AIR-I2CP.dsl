// Air14 IRP8 / SYNA2BA6: expose I2C resources for a polling-only trial.
// Rename only this machine's complete TPD0._CRS byte sequence to XCRS.
// Leave firmware initialization and OSYS intact; other OSes use XCRS.
DefinitionBlock ("", "SSDT", 2, "AIR14", "I2CPOLL", 0x00000001)
{
    External (\_SB.PC00.I2CM, MethodObj)
    External (\_SB.PC00.I2C0, DeviceObj)
    External (\_SB.PC00.I2C0.I2CX, IntObj)
    External (\_SB.PC00.I2C0.TPD0, DeviceObj)
    External (\_SB.PC00.I2C0.TPD0.BADR, FieldUnitObj)
    External (\_SB.PC00.I2C0.TPD0.SPED, FieldUnitObj)
    External (\_SB.PC00.I2C0.TPD0.XCRS, MethodObj)

    Scope (\_SB.PC00.I2C0.TPD0)
    {
        Method (_CRS, 0, Serialized)
        {
            If (_OSI ("Darwin"))
            {
                Return (\_SB.PC00.I2CM (\_SB.PC00.I2C0.I2CX, BADR, SPED))
            }
            Return (XCRS ())
        }
    }
}
