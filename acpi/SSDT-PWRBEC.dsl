/*
 * X-STATION V2 电源键修复：EC 就绪（_REG 3,1）后按字节读一遍 EC RAM 0x00-0xFF。
 * 实验结论（2026-10-28）：macOS 下 EC 不为电源键发 Query 0x54，除非 OS 读过一遍 EC RAM；
 * 开机读一次即可（v0.1.49 读一次后 4 分钟按键仍触发 _Q54），无需持续轮询。
 * 需配对 DSDT 改名：H_EC._REG -> XREG（Find 144A065F52454702，Replace 144A065852454702）。
 * 只读，不写 EC；只在 Darwin 下执行额外读取。
 */
DefinitionBlock ("", "SSDT", 2, "XSTN", "PWRBEC", 0x00000001)
{
    External (\_SB.PC00.LPCB.H_EC, DeviceObj)
    External (\_SB.PC00.LPCB.H_EC.XREG, MethodObj)

    Scope (\_SB.PC00.LPCB.H_EC)
    {
        OperationRegion (XECA, EmbeddedControl, Zero, 0x0100)
        Field (XECA, ByteAcc, Lock, Preserve)
        {
                E000,   8,
                E001,   8,
                E002,   8,
                E003,   8,
                E004,   8,
                E005,   8,
                E006,   8,
                E007,   8,
                E008,   8,
                E009,   8,
                E00A,   8,
                E00B,   8,
                E00C,   8,
                E00D,   8,
                E00E,   8,
                E00F,   8,
                E010,   8,
                E011,   8,
                E012,   8,
                E013,   8,
                E014,   8,
                E015,   8,
                E016,   8,
                E017,   8,
                E018,   8,
                E019,   8,
                E01A,   8,
                E01B,   8,
                E01C,   8,
                E01D,   8,
                E01E,   8,
                E01F,   8,
                E020,   8,
                E021,   8,
                E022,   8,
                E023,   8,
                E024,   8,
                E025,   8,
                E026,   8,
                E027,   8,
                E028,   8,
                E029,   8,
                E02A,   8,
                E02B,   8,
                E02C,   8,
                E02D,   8,
                E02E,   8,
                E02F,   8,
                E030,   8,
                E031,   8,
                E032,   8,
                E033,   8,
                E034,   8,
                E035,   8,
                E036,   8,
                E037,   8,
                E038,   8,
                E039,   8,
                E03A,   8,
                E03B,   8,
                E03C,   8,
                E03D,   8,
                E03E,   8,
                E03F,   8,
                E040,   8,
                E041,   8,
                E042,   8,
                E043,   8,
                E044,   8,
                E045,   8,
                E046,   8,
                E047,   8,
                E048,   8,
                E049,   8,
                E04A,   8,
                E04B,   8,
                E04C,   8,
                E04D,   8,
                E04E,   8,
                E04F,   8,
                E050,   8,
                E051,   8,
                E052,   8,
                E053,   8,
                E054,   8,
                E055,   8,
                E056,   8,
                E057,   8,
                E058,   8,
                E059,   8,
                E05A,   8,
                E05B,   8,
                E05C,   8,
                E05D,   8,
                E05E,   8,
                E05F,   8,
                E060,   8,
                E061,   8,
                E062,   8,
                E063,   8,
                E064,   8,
                E065,   8,
                E066,   8,
                E067,   8,
                E068,   8,
                E069,   8,
                E06A,   8,
                E06B,   8,
                E06C,   8,
                E06D,   8,
                E06E,   8,
                E06F,   8,
                E070,   8,
                E071,   8,
                E072,   8,
                E073,   8,
                E074,   8,
                E075,   8,
                E076,   8,
                E077,   8,
                E078,   8,
                E079,   8,
                E07A,   8,
                E07B,   8,
                E07C,   8,
                E07D,   8,
                E07E,   8,
                E07F,   8,
                E080,   8,
                E081,   8,
                E082,   8,
                E083,   8,
                E084,   8,
                E085,   8,
                E086,   8,
                E087,   8,
                E088,   8,
                E089,   8,
                E08A,   8,
                E08B,   8,
                E08C,   8,
                E08D,   8,
                E08E,   8,
                E08F,   8,
                E090,   8,
                E091,   8,
                E092,   8,
                E093,   8,
                E094,   8,
                E095,   8,
                E096,   8,
                E097,   8,
                E098,   8,
                E099,   8,
                E09A,   8,
                E09B,   8,
                E09C,   8,
                E09D,   8,
                E09E,   8,
                E09F,   8,
                E0A0,   8,
                E0A1,   8,
                E0A2,   8,
                E0A3,   8,
                E0A4,   8,
                E0A5,   8,
                E0A6,   8,
                E0A7,   8,
                E0A8,   8,
                E0A9,   8,
                E0AA,   8,
                E0AB,   8,
                E0AC,   8,
                E0AD,   8,
                E0AE,   8,
                E0AF,   8,
                E0B0,   8,
                E0B1,   8,
                E0B2,   8,
                E0B3,   8,
                E0B4,   8,
                E0B5,   8,
                E0B6,   8,
                E0B7,   8,
                E0B8,   8,
                E0B9,   8,
                E0BA,   8,
                E0BB,   8,
                E0BC,   8,
                E0BD,   8,
                E0BE,   8,
                E0BF,   8,
                E0C0,   8,
                E0C1,   8,
                E0C2,   8,
                E0C3,   8,
                E0C4,   8,
                E0C5,   8,
                E0C6,   8,
                E0C7,   8,
                E0C8,   8,
                E0C9,   8,
                E0CA,   8,
                E0CB,   8,
                E0CC,   8,
                E0CD,   8,
                E0CE,   8,
                E0CF,   8,
                E0D0,   8,
                E0D1,   8,
                E0D2,   8,
                E0D3,   8,
                E0D4,   8,
                E0D5,   8,
                E0D6,   8,
                E0D7,   8,
                E0D8,   8,
                E0D9,   8,
                E0DA,   8,
                E0DB,   8,
                E0DC,   8,
                E0DD,   8,
                E0DE,   8,
                E0DF,   8,
                E0E0,   8,
                E0E1,   8,
                E0E2,   8,
                E0E3,   8,
                E0E4,   8,
                E0E5,   8,
                E0E6,   8,
                E0E7,   8,
                E0E8,   8,
                E0E9,   8,
                E0EA,   8,
                E0EB,   8,
                E0EC,   8,
                E0ED,   8,
                E0EE,   8,
                E0EF,   8,
                E0F0,   8,
                E0F1,   8,
                E0F2,   8,
                E0F3,   8,
                E0F4,   8,
                E0F5,   8,
                E0F6,   8,
                E0F7,   8,
                E0F8,   8,
                E0F9,   8,
                E0FA,   8,
                E0FB,   8,
                E0FC,   8,
                E0FD,   8,
                E0FE,   8,
                E0FF,   8
        }

        Method (_REG, 2, NotSerialized)  // _REG: Region Availability
        {
            XREG (Arg0, Arg1)
            If (LAnd (_OSI ("Darwin"), LAnd (LEqual (Arg0, 0x03), LEqual (Arg1, One))))
            {
                XECI ()
            }
        }

        Method (XECI, 0, Serialized)
        {
                Store (E000, Local0)
                Store (E001, Local0)
                Store (E002, Local0)
                Store (E003, Local0)
                Store (E004, Local0)
                Store (E005, Local0)
                Store (E006, Local0)
                Store (E007, Local0)
                Store (E008, Local0)
                Store (E009, Local0)
                Store (E00A, Local0)
                Store (E00B, Local0)
                Store (E00C, Local0)
                Store (E00D, Local0)
                Store (E00E, Local0)
                Store (E00F, Local0)
                Store (E010, Local0)
                Store (E011, Local0)
                Store (E012, Local0)
                Store (E013, Local0)
                Store (E014, Local0)
                Store (E015, Local0)
                Store (E016, Local0)
                Store (E017, Local0)
                Store (E018, Local0)
                Store (E019, Local0)
                Store (E01A, Local0)
                Store (E01B, Local0)
                Store (E01C, Local0)
                Store (E01D, Local0)
                Store (E01E, Local0)
                Store (E01F, Local0)
                Store (E020, Local0)
                Store (E021, Local0)
                Store (E022, Local0)
                Store (E023, Local0)
                Store (E024, Local0)
                Store (E025, Local0)
                Store (E026, Local0)
                Store (E027, Local0)
                Store (E028, Local0)
                Store (E029, Local0)
                Store (E02A, Local0)
                Store (E02B, Local0)
                Store (E02C, Local0)
                Store (E02D, Local0)
                Store (E02E, Local0)
                Store (E02F, Local0)
                Store (E030, Local0)
                Store (E031, Local0)
                Store (E032, Local0)
                Store (E033, Local0)
                Store (E034, Local0)
                Store (E035, Local0)
                Store (E036, Local0)
                Store (E037, Local0)
                Store (E038, Local0)
                Store (E039, Local0)
                Store (E03A, Local0)
                Store (E03B, Local0)
                Store (E03C, Local0)
                Store (E03D, Local0)
                Store (E03E, Local0)
                Store (E03F, Local0)
                Store (E040, Local0)
                Store (E041, Local0)
                Store (E042, Local0)
                Store (E043, Local0)
                Store (E044, Local0)
                Store (E045, Local0)
                Store (E046, Local0)
                Store (E047, Local0)
                Store (E048, Local0)
                Store (E049, Local0)
                Store (E04A, Local0)
                Store (E04B, Local0)
                Store (E04C, Local0)
                Store (E04D, Local0)
                Store (E04E, Local0)
                Store (E04F, Local0)
                Store (E050, Local0)
                Store (E051, Local0)
                Store (E052, Local0)
                Store (E053, Local0)
                Store (E054, Local0)
                Store (E055, Local0)
                Store (E056, Local0)
                Store (E057, Local0)
                Store (E058, Local0)
                Store (E059, Local0)
                Store (E05A, Local0)
                Store (E05B, Local0)
                Store (E05C, Local0)
                Store (E05D, Local0)
                Store (E05E, Local0)
                Store (E05F, Local0)
                Store (E060, Local0)
                Store (E061, Local0)
                Store (E062, Local0)
                Store (E063, Local0)
                Store (E064, Local0)
                Store (E065, Local0)
                Store (E066, Local0)
                Store (E067, Local0)
                Store (E068, Local0)
                Store (E069, Local0)
                Store (E06A, Local0)
                Store (E06B, Local0)
                Store (E06C, Local0)
                Store (E06D, Local0)
                Store (E06E, Local0)
                Store (E06F, Local0)
                Store (E070, Local0)
                Store (E071, Local0)
                Store (E072, Local0)
                Store (E073, Local0)
                Store (E074, Local0)
                Store (E075, Local0)
                Store (E076, Local0)
                Store (E077, Local0)
                Store (E078, Local0)
                Store (E079, Local0)
                Store (E07A, Local0)
                Store (E07B, Local0)
                Store (E07C, Local0)
                Store (E07D, Local0)
                Store (E07E, Local0)
                Store (E07F, Local0)
                Store (E080, Local0)
                Store (E081, Local0)
                Store (E082, Local0)
                Store (E083, Local0)
                Store (E084, Local0)
                Store (E085, Local0)
                Store (E086, Local0)
                Store (E087, Local0)
                Store (E088, Local0)
                Store (E089, Local0)
                Store (E08A, Local0)
                Store (E08B, Local0)
                Store (E08C, Local0)
                Store (E08D, Local0)
                Store (E08E, Local0)
                Store (E08F, Local0)
                Store (E090, Local0)
                Store (E091, Local0)
                Store (E092, Local0)
                Store (E093, Local0)
                Store (E094, Local0)
                Store (E095, Local0)
                Store (E096, Local0)
                Store (E097, Local0)
                Store (E098, Local0)
                Store (E099, Local0)
                Store (E09A, Local0)
                Store (E09B, Local0)
                Store (E09C, Local0)
                Store (E09D, Local0)
                Store (E09E, Local0)
                Store (E09F, Local0)
                Store (E0A0, Local0)
                Store (E0A1, Local0)
                Store (E0A2, Local0)
                Store (E0A3, Local0)
                Store (E0A4, Local0)
                Store (E0A5, Local0)
                Store (E0A6, Local0)
                Store (E0A7, Local0)
                Store (E0A8, Local0)
                Store (E0A9, Local0)
                Store (E0AA, Local0)
                Store (E0AB, Local0)
                Store (E0AC, Local0)
                Store (E0AD, Local0)
                Store (E0AE, Local0)
                Store (E0AF, Local0)
                Store (E0B0, Local0)
                Store (E0B1, Local0)
                Store (E0B2, Local0)
                Store (E0B3, Local0)
                Store (E0B4, Local0)
                Store (E0B5, Local0)
                Store (E0B6, Local0)
                Store (E0B7, Local0)
                Store (E0B8, Local0)
                Store (E0B9, Local0)
                Store (E0BA, Local0)
                Store (E0BB, Local0)
                Store (E0BC, Local0)
                Store (E0BD, Local0)
                Store (E0BE, Local0)
                Store (E0BF, Local0)
                Store (E0C0, Local0)
                Store (E0C1, Local0)
                Store (E0C2, Local0)
                Store (E0C3, Local0)
                Store (E0C4, Local0)
                Store (E0C5, Local0)
                Store (E0C6, Local0)
                Store (E0C7, Local0)
                Store (E0C8, Local0)
                Store (E0C9, Local0)
                Store (E0CA, Local0)
                Store (E0CB, Local0)
                Store (E0CC, Local0)
                Store (E0CD, Local0)
                Store (E0CE, Local0)
                Store (E0CF, Local0)
                Store (E0D0, Local0)
                Store (E0D1, Local0)
                Store (E0D2, Local0)
                Store (E0D3, Local0)
                Store (E0D4, Local0)
                Store (E0D5, Local0)
                Store (E0D6, Local0)
                Store (E0D7, Local0)
                Store (E0D8, Local0)
                Store (E0D9, Local0)
                Store (E0DA, Local0)
                Store (E0DB, Local0)
                Store (E0DC, Local0)
                Store (E0DD, Local0)
                Store (E0DE, Local0)
                Store (E0DF, Local0)
                Store (E0E0, Local0)
                Store (E0E1, Local0)
                Store (E0E2, Local0)
                Store (E0E3, Local0)
                Store (E0E4, Local0)
                Store (E0E5, Local0)
                Store (E0E6, Local0)
                Store (E0E7, Local0)
                Store (E0E8, Local0)
                Store (E0E9, Local0)
                Store (E0EA, Local0)
                Store (E0EB, Local0)
                Store (E0EC, Local0)
                Store (E0ED, Local0)
                Store (E0EE, Local0)
                Store (E0EF, Local0)
                Store (E0F0, Local0)
                Store (E0F1, Local0)
                Store (E0F2, Local0)
                Store (E0F3, Local0)
                Store (E0F4, Local0)
                Store (E0F5, Local0)
                Store (E0F6, Local0)
                Store (E0F7, Local0)
                Store (E0F8, Local0)
                Store (E0F9, Local0)
                Store (E0FA, Local0)
                Store (E0FB, Local0)
                Store (E0FC, Local0)
                Store (E0FD, Local0)
                Store (E0FE, Local0)
                Store (E0FF, Local0)
        }
    }
}
