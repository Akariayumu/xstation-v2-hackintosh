/*
 * X-STATION V2: PNLF under the real IGPU ACPI path (\_SB.PC00.GFX0).
 * Replaces the root-scope Device (PNLF) that macOS never enumerates.
 * _UID 19 selects the WhateverGreen/WhateverXe 16-bit PWM profile.
 */
DefinitionBlock ("", "SSDT", 2, "XSTN", "PNLF", 0x00000000)
{
    External (_SB_.PC00.GFX0, DeviceObj)

    Scope (\_SB.PC00.GFX0)
    {
        Device (PNLF)
        {
            Name (_HID, EisaId ("APP0002"))
            Name (_CID, "backlight")
            Name (_UID, 0x13)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0B)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }
    }
}
