/*
 * X-STATION V2 Battery Full Threshold Fix for S0ix AC Standby
 *
 * 作用：
 *   X-STATION V2 的原厂 EC（\_SB.PC00.LPCB.H_EC.BAT0）在外接电源充满停充时，
 *   BPCN 停在 95（即 _BST[2] = 39281 mWh，而 _BIF[2] Last Full Charge Capacity = 41349 mWh，
 *   占比 94.998%），且 _BST[0] 返回非充电也非放电（(State & 0x03) == 0）。
 *   VirtualSMC / SMCBatteryManager 仅当 Remaining >= LastFull 时才置位 FullyCharged = Yes，
 *   导致 macOS 认为外接电源下电池处于“未充满但无故停止充电”的异常状态，进而触发 powerd
 *   （Invalid notChargingReason / GaugingMitigationActions）与 PowerUIAgent 周期性 DarkWake。
 *
 *   本表仅在 Darwin（_OSI ("Darwin")）下生效：当 _BST 处于非充非放稳态（(Local1 & 0x03) == 0）
 *   且剩余容量达到最后满充容量 _BIF()[2] 的 94% 及以上（Local2 * 100 >= Local4 * 94）时，
 *   返回剩余容量等于 _BIF()[2] 的 FBST，使 macOS 正常识别 FullyCharged = Yes；
 *   充电中、放电中或非 Darwin 系统（Windows / Linux）100% 原样透传 XBST()。
 *
 * 必须配对的 OpenCore ACPI -> Patch 补丁（DSDT, OemTableId = "A M I \0\0" / 41204D2049200000）：
 *   Rename BAT0._BST to XBST:
 *     Find:    14 49 0A 5F 42 53 54 00 08 50 4B 47 31
 *     Replace: 14 49 0A 58 42 53 54 00 08 50 4B 47 31
 *     Count:   1  (dsdt.dat 偏移 0x73EA，全表及 19 张 SSDT 唯一)
 */
DefinitionBlock ("", "SSDT", 2, "XSTA", "BATFULL", 0x00001000)
{
    External (\_SB.PC00.LPCB.H_EC.BAT0, DeviceObj)
    External (\_SB.PC00.LPCB.H_EC.BAT0.XBST, MethodObj)
    External (\_SB.PC00.LPCB.H_EC.BAT0._BIF, MethodObj)

    Scope (\_SB.PC00.LPCB.H_EC.BAT0)
    {
        Name (FBST, Package (0x04)
        {
            Zero,
            Zero,
            Zero,
            Zero
        })

        Method (_BST, 0, Serialized)  // _BST: Battery Status
        {
            Local0 = XBST ()
            If (!_OSI ("Darwin"))
            {
                Return (Local0)
            }

            Local1 = DerefOf (Local0 [Zero])
            If ((Local1 & 0x03))
            {
                Return (Local0)
            }

            Local2 = DerefOf (Local0 [0x02])
            Local3 = _BIF ()
            Local4 = DerefOf (Local3 [0x02])
            If (((Local2 == Zero) || ((Local2 >= 0x7FFFFFFF) || ((Local4 == Zero) ||
                ((Local4 >= 0x7FFFFFFF) || (Local2 >= Local4))))))
            {
                Return (Local0)
            }

            If (((Local2 * 0x64) >= (Local4 * 0x5E)))
            {
                FBST [Zero] = Local1
                FBST [One] = DerefOf (Local0 [One])
                FBST [0x02] = Local4
                FBST [0x03] = DerefOf (Local0 [0x03])
                Return (FBST)
            }

            Return (Local0)
        }
    }
}
