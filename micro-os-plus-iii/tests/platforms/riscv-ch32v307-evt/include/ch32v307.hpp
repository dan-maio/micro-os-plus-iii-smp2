/*
 * ch32v307.hpp -- the CH32V307 (QingKe V4F, RV32IMAFC) registers this board
 * uses. Addresses and bits from the WCH manuals (CH32FV2x_V3xRM V2.5,
 * QingKeV4 Processor Manual V1.5) and ch32v30x.h of the WCH EVT.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace ch32
{
  inline volatile std::uint32_t&
  reg (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

  // RCC (RM 3.4)
  inline constexpr std::uintptr_t RCC_CTLR = 0x40021000u;
  inline constexpr std::uintptr_t RCC_CFGR0 = 0x40021004u;
  inline constexpr std::uintptr_t RCC_INTR = 0x40021008u;
  inline constexpr std::uintptr_t RCC_APB2PCENR = 0x40021018u;
  inline constexpr std::uintptr_t RCC_CFGR2 = 0x4002102Cu;

  inline constexpr std::uint32_t RCC_HSION = 1u << 0;
  inline constexpr std::uint32_t RCC_HSEON = 1u << 16;
  inline constexpr std::uint32_t RCC_HSERDY = 1u << 17;
  inline constexpr std::uint32_t RCC_PLLON = 1u << 24;
  inline constexpr std::uint32_t RCC_PLLRDY = 1u << 25;
  inline constexpr std::uint32_t RCC_SW_MASK = 3u << 0;
  inline constexpr std::uint32_t RCC_SW_PLL = 2u << 0;
  inline constexpr std::uint32_t RCC_SWS_MASK = 3u << 2;
  inline constexpr std::uint32_t RCC_SWS_PLL = 2u << 2;
  inline constexpr std::uint32_t RCC_PPRE1_DIV2 = 4u << 8;
  inline constexpr std::uint32_t RCC_PLLSRC_HSE = 1u << 16;
  // PLLSRC | PLLXTPRE | PLLMUL[3:0]
  inline constexpr std::uint32_t RCC_PLL_MASK = 0x003F0000u;
  // D8C (CH32V307): PLLMUL 0000 = x18 (RM, RCC_CFGR0)
  inline constexpr std::uint32_t RCC_PLLMUL18_D8C = 0u << 18;

  inline constexpr std::uint32_t RCC_IOPAEN = 1u << 2;
  inline constexpr std::uint32_t RCC_IOPDEN = 1u << 5;
  inline constexpr std::uint32_t RCC_USART1EN = 1u << 14;

  // GPIO (RM 10.3): 4 bits per pin, CNF[1:0] MODE[1:0]
  inline constexpr std::uintptr_t GPIOA = 0x40010800u;
  inline constexpr std::uintptr_t GPIOD = 0x40011400u;
  inline constexpr std::uintptr_t GPIO_CFGLR = 0x00u;
  inline constexpr std::uintptr_t GPIO_CFGHR = 0x04u;
  inline constexpr std::uintptr_t GPIO_INDR = 0x08u;
  inline constexpr std::uintptr_t GPIO_OUTDR = 0x0Cu;
  inline constexpr std::uintptr_t GPIO_BSHR = 0x10u;
  inline constexpr std::uintptr_t GPIO_BCR = 0x14u;
  inline constexpr std::uint32_t GPIO_IN_FLOATING = 0x4u;  // CNF 01 MODE 00
  inline constexpr std::uint32_t GPIO_IN_PULL = 0x8u;      // CNF 10 MODE 00
  inline constexpr std::uint32_t GPIO_OUT_PP_2MHZ = 0x2u;  // CNF 00 MODE 10
  inline constexpr std::uint32_t GPIO_AF_PP_50MHZ = 0xBu;  // CNF 10 MODE 11

  // USART1 on APB2 (RM 18.10)
  inline constexpr std::uintptr_t USART1_STATR = 0x40013800u;
  inline constexpr std::uintptr_t USART1_DATAR = 0x40013804u;
  inline constexpr std::uintptr_t USART1_BRR = 0x40013808u;
  inline constexpr std::uintptr_t USART1_CTLR1 = 0x4001380Cu;
  inline constexpr std::uint32_t USART_RXNE = 1u << 5;
  inline constexpr std::uint32_t USART_TC = 1u << 6;
  inline constexpr std::uint32_t USART_TXE = 1u << 7;
  inline constexpr std::uint32_t USART_RE = 1u << 2;
  inline constexpr std::uint32_t USART_TE = 1u << 3;
  inline constexpr std::uint32_t USART_UE = 1u << 13;

  // SysTick, 64-bit (QingKe V4 manual, chapter 5)
  inline constexpr std::uintptr_t STK_CTLR = 0xE000F000u;
  inline constexpr std::uintptr_t STK_SR = 0xE000F004u;
  inline constexpr std::uintptr_t STK_CNTL = 0xE000F008u;
  inline constexpr std::uintptr_t STK_CNTH = 0xE000F00Cu;
  inline constexpr std::uintptr_t STK_CMPLR = 0xE000F010u;
  inline constexpr std::uintptr_t STK_CMPHR = 0xE000F014u;
  inline constexpr std::uint32_t STK_STE = 1u << 0;
  inline constexpr std::uint32_t STK_STIE = 1u << 1;
  inline constexpr std::uint32_t STK_STCLK_HCLK = 1u << 2;
  inline constexpr std::uint32_t STK_STRE = 1u << 3;
  inline constexpr std::uint32_t STK_INIT = 1u << 5;
  inline constexpr std::uint32_t STK_SWIE = 1u << 31;

  // Vendor CSRs (QingKe V4 manual, chapter 8)
  inline constexpr unsigned CSR_GINTENR = 0x800u;
  inline constexpr unsigned CSR_INTSYSCR = 0x804u;
  inline constexpr unsigned CSR_CORECFGR = 0xBC0u;

  // ESIG (RM 31.2)
  inline constexpr std::uintptr_t ESIG_FLACAP = 0x1FFFF7E0u;
  inline constexpr std::uintptr_t DBG_IDCODE = 0x1FFFF704u;

  // The SysTick counter, 64 bits read consistently (high, low, high).
  inline std::uint64_t
  systick_now (void)
  {
    std::uint32_t hi;
    std::uint32_t lo;
    do
      {
        hi = reg (STK_CNTH);
        lo = reg (STK_CNTL);
      }
    while (hi != reg (STK_CNTH));
    return (static_cast<std::uint64_t> (hi) << 32) | lo;
  }
} // namespace ch32
