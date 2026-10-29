/*
 * SSDT-HIDDSTA-v2：在 macOS 下让 \_SB.HIDD（Intel HID 事件设备）的 _STA 返回 0x0F，
 * 其他系统沿用原 _STA。需配合 config 里 ACPI → Patch 的 HIDD._STA → XSTA 改名。
 * 由 EFI 中的 AML 反编译整理（iasl -d），与发布的 SSDT-HIDDSTA-v2.aml 内容一致。
 */
DefinitionBlock ("", "SSDT", 2, "XSTN", "HIDDSTA", 0x00000002)
{
    External (_SB_.HIDD, DeviceObj)
    External (_SB_.HIDD.XSTA, MethodObj)    // 0 Arguments

    Scope (\_SB.HIDD)
    {
        Method (_STA, 0, NotSerialized)  // _STA: Status
        {
            If (_OSI ("Darwin"))
            {
                Return (0x0F)
            }

            Return (\_SB.HIDD.XSTA ())
        }
    }
}

