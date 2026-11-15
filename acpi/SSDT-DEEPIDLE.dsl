/*
 * X-STATION V2 S0ix Deep Idle Enabler + Native \_SB.PEPD Darwin Expose (Phase 1)
 *
 * 作用：
 *   1. 在 Darwin 下向系统暴露 \_SB.LPS0 = 1 与 \_GPE.LXEN = 1，使 AppleACPIPlatform
 *      发布 IOPMDeepIdleSupported = Yes，引导 XNU 选择原生 Deep Idle
 *      （IOPMSystemSleepType = 7，acpi_idle_kernel -> pmCPUHalt(PM_HALT_SLEEP)）。
 *   2. 在 Darwin 下覆盖原生 \_SB.PEPD._STA 返回 0x0F（原厂 DSDT 要求 OSYS >= 0x07DC，
 *      而本机不注入全局 SSDT-XOSI，macOS 下 OSYS == 0x03E8 导致原生 PEPD._STA 返回 0），
 *      使 S0ixStandby.kext（IONameMatch = PNP0D80）可正常挂载并调用 PEPD._DSM。
 *      非 Darwin 系统（Windows / Linux）透传调用原厂 XSTA()，100% 零干扰。
 *
 * 必须配对的 OpenCore ACPI -> Patch 补丁（DSDT, OemTableId = "A M I \0\0" / QSBNIEkgAAA=）：
 *   1. Disable _S3 for S0ix: Name(_S3_) to XS3_
 *      Find:    08 5F 53 33 5F       (Base64: CF9TM18=)
 *      Replace: 08 58 53 33 5F       (Base64: CFhTM18=)
 *      Count:   1                    (dsdt.dat 偏移 0x6055，全表及 19 张 SSDT 唯一)
 *   2. Expose PEPD under Darwin: PEPD._STA to XSTA
 *      Find:    14 27 5F 53 54 41 00 (Base64: FCdfU1RBAA==)
 *      Replace: 14 27 58 53 54 41 00 (Base64: FCdYU1RBAA==)
 *      Count:   1                    (dsdt.dat 偏移 0x3B58D，全表及 19 张 SSDT 唯一)
 *
 * Phase 4 可选扩展（物理电源键通过 H_EC GPE 0x6E 唤醒；Phase 1-3 使用 RTC 定时唤醒期间暂不启用，
 * 详见 docs/research/s0ix-roadmap-20261115.md §3.4.2）：
 *
 *   External (\_SB.PWRB, DeviceObj)
 *   Scope (\_SB.PWRB)
 *   {
 *       If (_OSI ("Darwin"))
 *       {
 *           Name (PRW0, Package (0x02) { 0x6E, 0x04 })
 *       }
 *   }
 */
DefinitionBlock ("", "SSDT", 2, "XSTA", "DEEPIDLE", 0x00001000)
{
    External (\_SB.PEPD, DeviceObj)
    External (\_SB.PEPD.XSTA, MethodObj)

    Scope (\_SB)
    {
        Method (LPS0, 0, NotSerialized)
        {
            If (_OSI ("Darwin"))
            {
                Return (One)
            }

            Return (Zero)
        }
    }

    Scope (\_GPE)
    {
        Method (LXEN, 0, NotSerialized)
        {
            If (_OSI ("Darwin"))
            {
                Return (One)
            }

            Return (Zero)
        }
    }

    Scope (\_SB.PEPD)
    {
        Method (_STA, 0, NotSerialized)
        {
            If (_OSI ("Darwin"))
            {
                Return (0x0F)
            }

            Return (XSTA ())
        }
    }
}
