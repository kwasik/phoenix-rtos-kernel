/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * STM32L4 basic peripherals control functions
 *
 * Copyright 2020 Phoenix Systems
 * Author: Aleksander Kaminski, Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "stm32.h"
#include "interrupts.h"
#include "pmap.h"
#include "../../../include/errno.h"


struct {
	volatile u32 *rcc;
	volatile u32 *gpio[9];
	volatile u32 *pwr;
	volatile u32 *scb;
	volatile u32 *rtc;
	volatile u32 *nvic;
	volatile u32 *exti;
	volatile u32 *syst;
	volatile u32 *mpu;
	volatile u32 *syscfg;
	volatile u32 *iwdg;

	u32 cpuclk;

	u32 gpio_mode[8];
	u32 gpio_pupd[8];
	u32 uart_state[5];

	u32 resetFlags;

	spinlock_t pltctlSp;
} stm32_common;


enum { ahb1_begin = pctl_dma1, ahb1_end = pctl_dma2d, ahb2_begin = pctl_gpioa, ahb2_end = pctl_rng,
	ahb3_begin = pctl_fmc, ahb3_end = pctl_qspi, apb1_1_begin = pctl_tim2, apb1_1_end = pctl_lptim1,
	apb1_2_begin = pctl_lpuart1, apb1_2_end = pctl_lptim2, apb2_begin = pctl_syscfg, apb2_end = pctl_dfsdm1,
	misc_begin = pctl_rtc, misc_end = pctl_rtc };


enum { rcc_cr = 0, rcc_icscr, rcc_cfgr, rcc_pllcfgr, rcc_pllsai1cfgr, rcc_pllsai2cfgr, rcc_cier, rcc_cifr,
	rcc_cicr, rcc_ahb1rstr = rcc_cicr + 2, rcc_ahb2rstr, rcc_ahb3rstr, rcc_apb1rstr1 = rcc_ahb3rstr + 2,
	rcc_apb1rstr2, rcc_apb2rstr, rcc_ahb1enr = rcc_apb2rstr + 2, rcc_ahb2enr, rcc_ahb3enr,
	rcc_apb1enr1 = rcc_ahb3enr + 2, rcc_apb1enr2, rcc_apb2enr, rcc_ahb1smenr = rcc_apb2enr + 2,
	rcc_ahb2smenr, rcc_ahb3smenr, rcc_apb1smenr1 = rcc_ahb3smenr + 2, rcc_apb1smenr2, rcc_apb2smenr,
	rcc_ccipr = rcc_apb2smenr + 2, rcc_bdcr = rcc_ccipr + 2, rcc_csr, rcc_crrcr, rcc_ccipr2 };


enum { gpio_moder = 0, gpio_otyper, gpio_ospeedr, gpio_pupdr, gpio_idr,
	gpio_odr, gpio_bsrr, gpio_lckr, gpio_afrl, gpio_afrh, gpio_brr, gpio_ascr };


enum { pwr_cr1 = 0, pwr_cr2, pwr_cr3, pwr_cr4, pwr_sr1, pwr_sr2, pwr_scr, pwr_pucra, pwr_pdcra, pwr_pucrb,
	pwr_pdcrb, pwr_pucrc, pwr_pdcrc, pwr_pucrd, pwr_pdcrd, pwr_pucre, pwr_pdcre, pwr_pucrf, pwr_pdcrf,
	pwr_pucrg, pwr_pdcrg, pwr_pucrh, pwr_pdcrh, pwr_pucri, pwr_pdcri };


enum { rtc_tr = 0, rtc_dr, rtc_cr, rtc_isr, rtc_prer, rtc_wutr, rtc_alrmar = rtc_wutr + 2, rtc_alrmbr, rtc_wpr,
	rtc_ssr, rtc_shiftr, rtc_tstr, rtc_tsdr, rtc_tsssr, rtc_calr, rtc_tampcr, rtc_alrmassr, rtc_alrmbssr, rtc_or,
	rtc_bkpr };


enum { scb_actlr = 2, scb_cpuid = 3328, scb_icsr, scb_vtor, scb_aircr, scb_scr, scb_ccr, scb_shp1, scb_shp2,
	scb_shp3, scb_shcsr, scb_cfsr, scb_mmsr, scb_bfsr, scb_ufsr, scb_hfsr, scb_mmar, scb_bfar, scb_afsr };


enum { nvic_iser = 0, nvic_icer = 32, nvic_ispr = 64, nvic_icpr = 96, nvic_iabr = 128,
	nvic_ip = 192, nvic_stir = 896 };


enum { exti_imr1 = 0, exti_emr1, exti_rtsr1, exti_ftsr1, exti_swier1, exti_pr1, exti_imr2 = 8, exti_emr2,
	exti_rtsr2, exti_ftsr2, exti_swier2, exti_pr2 };


enum { syst_csr = 4, syst_rvr, syst_cvr, syst_calib };


enum { syscfg_memrmp = 0, syscfg_cfgr1, syscfg_exticr1, syscfg_exticr2, syscfg_exticr3, syscfg_exticr4,
	syscfg_scsr, syscfg_cfgr2, syscfg_swpr, syscfg_skr, syscfg_swpr2 };


enum { iwdg_kr = 0, iwdg_pr, iwdg_rlr, iwdg_sr, iwdg_winr };


/* platformctl syscall */


int hal_platformctl(void *ptr)
{
	platformctl_t *data = ptr;
	int ret = -EINVAL;

	hal_spinlockSet(&stm32_common.pltctlSp);

	switch (data->type) {
	case pctl_devclk:
		if (data->action == pctl_set)
			ret = _stm32_rccSetDevClock(data->devclk.dev, data->devclk.state);
		else if (data->action == pctl_get)
			ret = _stm32_rccGetDevClock(data->devclk.dev, &data->devclk.state);

		break;
	case pctl_cpuclk:
		if (data->action == pctl_set) {
			ret = _stm32_rccSetCPUClock(data->cpuclk.hz);
			_stm32_systickInit(SYSTICK_INTERVAL);
		}
		else if (data->action == pctl_get) {
			data->cpuclk.hz = _stm32_rccGetCPUClock();
			ret = EOK;
		}

		break;
	case pctl_reboot:
		if (data->action == pctl_set) {
			if (data->reboot.magic == PCTL_REBOOT_MAGIC)
				_stm32_nvicSystemReset();
		}
		else if (data->action == pctl_get) {
			data->reboot.reason = stm32_common.resetFlags;
		}
	}

	hal_spinlockClear(&stm32_common.pltctlSp);

	return ret;
}


void _stm32_platformInit(void)
{
	hal_spinlockCreate(&stm32_common.pltctlSp, "pltctl");
}


/* RCC (Reset and Clock Controller) */


int _stm32_rccSetDevClock(unsigned int d, u32 hz)
{
	u32 t;

	hz = !!hz;

	if (d <= ahb1_end) {
		t = *(stm32_common.rcc + rcc_ahb1enr) & ~(1 << (d - ahb1_begin));
		*(stm32_common.rcc + rcc_ahb1enr) = t | (hz << (d - ahb1_begin));
	}
	else if (d <= ahb2_end) {
		t = *(stm32_common.rcc + rcc_ahb2enr) & ~(1 << (d - ahb2_begin));
		*(stm32_common.rcc + rcc_ahb2enr) = t | (hz << (d - ahb2_begin));
	}
	else if (d <= ahb3_end) {
		t = *(stm32_common.rcc + rcc_ahb3enr) & ~(1 << (d - ahb3_begin));
		*(stm32_common.rcc + rcc_ahb3enr) = t | (hz << (d - ahb3_begin));
	}
	else if (d <= apb1_1_end) {
		t = *(stm32_common.rcc + rcc_apb1enr1) & ~(1 << (d - apb1_1_begin));
		*(stm32_common.rcc + rcc_apb1enr1) = t | (hz << (d - apb1_1_begin));
	}
	else if (d == pctl_rtc) {
		t = *(stm32_common.rcc + rcc_bdcr) & ~(1 << 15);
		*(stm32_common.rcc + rcc_bdcr) = t | (hz << 15);
	}
	else
		return -EINVAL;

	hal_cpuDataBarrier();

	return EOK;
}


int _stm32_rccGetDevClock(unsigned int d, u32 *hz)
{
	if (d <= ahb1_end)
		*hz = !!(*(stm32_common.rcc + rcc_ahb1enr) & (1 << d));
	else if (d <= ahb2_end)
		*hz = !!(*(stm32_common.rcc + rcc_ahb2enr) & (1 << (d - ahb2_begin)));
	else if (d <= ahb3_end)
		*hz = !!(*(stm32_common.rcc + rcc_ahb3enr) & (1 << (d - ahb3_begin)));
	else if (d == pctl_rtc)
		*hz = !!(*(stm32_common.rcc + rcc_bdcr) & (1 << 15));
	else
		return -EINVAL;

	return EOK;
}


int _stm32_rccSetCPUClock(u32 hz)
{
	u8 range;
	u32 t;

	if (hz <= 100 * 1000) {
		range = 0;
		hz = 100 * 1000;
	}
	else if (hz <= 200 * 1000) {
		range = 1;
		hz = 200 * 1000;
	}
	else if (hz <= 400 * 1000) {
		range = 2;
		hz = 400 * 1000;
	}
	else if (hz <= 800 * 1000) {
		range = 3;
		hz = 800 * 1000;
	}
	else if (hz <= 1000 * 1000) {
		range = 4;
		hz = 1000 * 1000;
	}
	else if (hz <= 2000 * 1000) {
		range = 5;
		hz = 2000 * 1000;
	}
	else if (hz <= 4000 * 1000) {
		range = 6;
		hz = 4000 * 1000;
	}
	else if (hz <= 8000 * 1000) {
		range = 7;
		hz = 8000 * 1000;
	}
	else if (hz <= 16000 * 1000) {
		range = 8;
		hz = 16000 * 1000;
	}
/* TODO - we need to change flash wait states to handle below frequencies
	else if (hz <= 24000 * 1000) {
		range = 9;
		hz = 24000 * 1000;
	}
	else if (hz <= 32000 * 1000) {
		range = 10;
		hz = 32000 * 1000;
	}
	else if (hz <= 48000 * 1000) {
		range = 11;
		hz = 48000 * 1000;
	}
*/
	else {
		/* We can use HSI, if higher frequency is needed */
		return -EINVAL;
	}

	if (hz > 6000 * 1000)
		_stm32_pwrSetCPUVolt(1);

	t = *(stm32_common.rcc + rcc_cr) & ~(0xf << 4);
	*(stm32_common.rcc + rcc_cr) = t | range << 4;
	hal_cpuDataBarrier();

	if (hz <= 6000 * 1000)
		_stm32_pwrSetCPUVolt(2);

	stm32_common.cpuclk = hz;

	return EOK;
}


u32 _stm32_rccGetCPUClock(void)
{
	return stm32_common.cpuclk;
}


void _stm32_rccClearResetFlags(void)
{
	*(stm32_common.rcc + rcc_csr) |= 1 << 23;
}


/* PWR */


void _stm32_pwrSetCPUVolt(u8 range)
{
	u32 t;

	if (range != 1 && range != 2)
		return;

	t = *(stm32_common.pwr + pwr_cr1) & ~(3 << 9);
	*(stm32_common.pwr + pwr_cr1) = t | (range << 9);

	/* Wait for VOSF flag */
	while (*(stm32_common.pwr + pwr_sr2) & (1 << 10));
}


void hal_preSleep(void)
{
	/* TODO */
}


void hal_postSleep(void)
{
	/* TODO */
}


int _stm32_pwrEnterLPStop(void)
{
/* TODO */
#if 0//def NDEBUG
	u8 lprun_state = !!(*(stm32_common.pwr + pwr_cr) & (1 << 14));
	u8 regulator_state = (*(stm32_common.pwr + pwr_csr) >> 11) & 3;
	int slept = 0, i;

	hal_preSleep();

	/* Set LPSDSR and ULP bits */
	*(stm32_common.pwr + pwr_cr) |= 1;
	*(stm32_common.pwr + pwr_cr) &= ~2;

	/* Set internal regulator to default range to further conserve power */
	_stm32_pwrSetCPUVolt(1);

	/* Set SLEEPDEEP bit of Cortex System Control Register */
	*(stm32_common.scb + scb_scr) |= 1 << 2;

	_stm32_rtcUnlockRegs();
	/* Set wakeup timer and interrupt bits */
	*(stm32_common.rtc + rtc_cr) |= (1 << 10) | (1 << 14);
	_stm32_rtcLockRegs();

	*(stm32_common.exti + exti_pr) |= 0xffffffff;

	/* Enter Stop mode */
	__asm__ volatile ("\
		dmb; \
		wfe; \
		nop; ");

	/* Find out if device actually woke up because of the alarm */
	slept = !!(*(stm32_common.pwr + pwr_csr) & 1);

	/* Reset SLEEPDEEP bit of Cortex System Control Register */
	*(stm32_common.scb + scb_scr) &= ~(1 << 2);

	/* Reset LPSDSR and ULP bits */
	*(stm32_common.pwr + pwr_cr) &= ~1;

	/* Clear standby and wakeup flags */
	*(stm32_common.pwr + pwr_cr) |= (3 << 2) | 1;

	/* Clear wakeup timer and interrupt bits */
	*(stm32_common.rtc + rtc_cr) &= ~((1 << 10) | (1 << 14));

	hal_postSleep();

	/* Recover previous configuration */
	_stm32_pwrSetCPUVolt(regulator_state);
	_stm32_rccSetCPUClock(stm32_common.cpuclk);
	_stm32_pwrEnterLPRun(lprun_state);

	return slept;
#else
	return 0;
#endif
}


/* RTC */


void _stm32_rtcUnlockRegs(void)
{
	/* Set DBP bit */
	*(stm32_common.pwr + pwr_cr1) |= 1 << 8;

	/* Unlock RTC */
	*(stm32_common.rtc + rtc_wpr) = 0x000000ca;
	*(stm32_common.rtc + rtc_wpr) = 0x00000053;
}


void _stm32_rtcLockRegs(void)
{
	/* Lock RTC */
	*(stm32_common.rtc + rtc_wpr) = 0x000000ff;

	/* Reset DBP bit */
	*(stm32_common.pwr + pwr_cr1) &= ~(1 << 8);
}


void _stm32_rtcSetAlarm(u32 ms)
{
	_stm32_rtcUnlockRegs();

	/* Clear WUTF flag */
	*(stm32_common.rtc + rtc_isr) &= ~(1 << 10);

	/* Clear sleep status (CSBF and CWUF bits) */
	*(stm32_common.pwr + pwr_scr) |= 0x11f;

	/* Clear wakeup timer and interrupt bits */
	*(stm32_common.rtc + rtc_cr) &= ~((1 << 10) | (1 << 14));

	/* Wait for WUTWF flag */
	while (!(*(stm32_common.rtc + rtc_isr) & (1 << 2)));

	/* Load wakeup timer register */
	*(stm32_common.rtc + rtc_wutr) = (ms << 1) & 0xffff;

	/* Select RTC/16 wakeup clock */
	*(stm32_common.rtc + rtc_cr) &= ~0x7;

	/* Unmask interrupt */
	_stm32_extiMaskEvent(20, 1);

	/* Set rising edge trigger */
	_stm32_extiSetTrigger(20, 1, 1);

	_stm32_rtcLockRegs();
}


/* SCB */


void _stm32_scbSetPriorityGrouping(u32 group)
{
	u32 t;

	/* Get register value and clear bits to set */
	t = *(stm32_common.scb + scb_aircr) & ~0xffff0700;

	/* Store new value */
	*(stm32_common.scb + scb_aircr) = t | 0x5fa0000 | ((group & 7) << 8);
}


u32 _stm32_scbGetPriorityGrouping(void)
{
	return (*(stm32_common.scb + scb_aircr) & 0x700) >> 8;
}


void _stm32_scbSetPriority(s8 excpn, u32 priority)
{
	volatile u8 *ptr;

	ptr = &((u8*)(stm32_common.scb + scb_shp1))[excpn - 4];

	*ptr = (priority << 4) & 0x0ff;
}


u32 _stm32_scbGetPriority(s8 excpn)
{
	volatile u8 *ptr;

	ptr = &((u8*)(stm32_common.scb + scb_shp1))[excpn - 4];

	return *ptr >> 4;
}


/* NVIC (Nested Vectored Interrupt Controller */


void _stm32_nvicSetIRQ(s8 irqn, u8 state)
{
	volatile u32 *ptr = stm32_common.nvic + ((u8)irqn >> 5) + (state ? nvic_iser: nvic_icer);
	*ptr |= 1 << (irqn & 0x1F);
}


u32 _stm32_nvicGetPendingIRQ(s8 irqn)
{
	volatile u32 *ptr = stm32_common.nvic + ((u8)irqn >> 5) + nvic_ispr;
	return !!(*ptr & (1 << (irqn & 0x1F)));
}


void _stm32_nvicSetPendingIRQ(s8 irqn, u8 state)
{
	volatile u32 *ptr = stm32_common.nvic + ((u8)irqn >> 5) + (state ? nvic_ispr: nvic_icpr);
	*ptr |= 1 << (irqn & 0x1F);
}


u32 _stm32_nvicGetActive(s8 irqn)
{
	volatile u32 *ptr = stm32_common.nvic + ((u8)irqn >> 5) + nvic_iabr;
	return !!(*ptr & (1 << (irqn & 0x1F)));
}


void _stm32_nvicSetPriority(s8 irqn, u32 priority)
{
	volatile u8 *ptr;

	ptr = ((u8*)(stm32_common.nvic + nvic_ip)) + irqn;

	*ptr = (priority << 4) & 0x0ff;
}


u8 _stm32_nvicGetPriority(s8 irqn)
{
	volatile u8 *ptr;

	ptr = ((u8*)(stm32_common.nvic + nvic_ip)) + irqn;

	return *ptr >> 4;
}


void _stm32_nvicSystemReset(void)
{
	*(stm32_common.scb + scb_aircr) = ((0x5fa << 16) | (*(stm32_common.scb + scb_aircr) & (0x700)) | (1 << 0x02));

	__asm__ volatile ("dsb");

	for(;;);
}


/* EXTI */


int _stm32_extiMaskInterrupt(u32 line, u8 state)
{
	u32 t;
	volatile u32 *base = line > 32 ? stm32_common.exti + exti_imr1 : stm32_common.exti + exti_imr2;

	if (line > 40)
		return -EINVAL;
	else if (line >= 32)
		line -= 32;

	t = *base & ~(!state << line);
	*base = t | !!state << line;

	return EOK;
}


int _stm32_extiMaskEvent(u32 line, u8 state)
{
	u32 t;
	volatile u32 *base = line > 32 ? stm32_common.exti + exti_emr1 : stm32_common.exti + exti_emr2;

	if (line > 40)
		return -EINVAL;
	else if (line >= 32)
		line -= 32;

	t = *base & ~(!state << line);
	*base = t | !!state << line;

	return EOK;
}


int _stm32_extiSetTrigger(u32 line, u8 state, u8 edge)
{
	volatile u32 *p;
	const int reglut[2][2] = { { exti_rtsr1, exti_ftsr1 }, { exti_rtsr2, exti_ftsr2 } };

	if (line > 40)
		return -EINVAL;

	p = stm32_common.exti + reglut[line >= 32][!!edge];

	if (line >= 32)
		line -= 32;

	*p &= ~(!state << line);
	*p |= !!state << line;

	return EOK;
}


int _stm32_extiSoftInterrupt(u32 line)
{
	if (line > 40)
		return -EINVAL;

	if (line < 32)
		*(stm32_common.exti + exti_swier1) |= 1 << line;
	else
		*(stm32_common.exti + exti_swier2) |= 1 << (line - 32);

	return EOK;
}


/* SysTick */


int _stm32_systickInit(u32 interval)
{
	u64 load = ((u64) interval * stm32_common.cpuclk) / 1000000;
	if (load > 0x00ffffff)
		return -EINVAL;

	*(stm32_common.syst + syst_rvr) = (u32)load;
	*(stm32_common.syst + syst_cvr) = 0;

	/* Enable systick */
	*(stm32_common.syst + syst_csr) |= 0x7;

	return EOK;
}


u32 _stm32_systickGet(void)
{
	u32 cb;

	cb = ((*(stm32_common.syst + syst_rvr) - *(stm32_common.syst + syst_cvr)) * 1000) / *(stm32_common.syst + syst_rvr);

	/* Add 1000 us if there's systick pending */
	if (*(stm32_common.scb + scb_icsr) & (1 << 26))
		cb += 1000;

	return cb;
}


/* GPIO */


int _stm32_gpioConfig(unsigned int d, u8 pin, u8 mode, u8 af, u8 otype, u8 ospeed, u8 pupd)
{
	volatile u32 *base;
	u32 t;

	if (d > pctl_gpioh || pin > 15)
		return -EINVAL;

	base = stm32_common.gpio[d - pctl_gpioa];

	t = *(base + gpio_moder) & ~(0x3 << (pin << 1));
	*(base + gpio_moder) = t | (mode & 0x3) << (pin << 1);

	t = *(base + gpio_otyper) & ~(1 << pin);
	*(base + gpio_otyper) = t | (otype & 1) << pin;

	t = *(base + gpio_ospeedr) & ~(0x3 << (pin << 1));
	*(base + gpio_ospeedr) = t | (ospeed & 0x3) << (pin << 1);

	t = *(base + gpio_pupdr) & ~(0x03 << (pin << 1));
	*(base + gpio_pupdr) = t | (pupd & 0x3) << (pin << 1);

	if (pin < 8) {
		t = *(base + gpio_afrl) & ~(0xf << (pin << 2));
		*(base + gpio_afrl) = t | (af & 0xf) << (pin << 2);
	}
	else {
		t = *(base + gpio_afrh) & ~(0xf << ((pin - 8) << 2));
		*(base + gpio_afrh) = t | (af & 0xf) << ((pin - 8) << 2);
	}

	if (mode == 0x3)
		*(base + gpio_ascr) |= 1 << pin;
	else
		*(base + gpio_ascr) &= ~(1 << pin);

	return EOK;
}


int _stm32_gpioSet(unsigned int d, u8 pin, u8 val)
{
	volatile u32 *base;
	u32 t;

	if (d > pctl_gpioh || pin > 15)
		return -EINVAL;

	base = stm32_common.gpio[d - pctl_gpioa];

	t = *(base + gpio_odr) & ~(!(u32)val << pin);
	*(base + gpio_odr) = t | !!(u32)val << pin;

	return EOK;
}


int _stm32_gpioSetPort(unsigned int d, u16 val)
{
	volatile u32 *base;

	if (d > pctl_gpioh)
		return -EINVAL;

	base = stm32_common.gpio[d - pctl_gpioa];
	*(base + gpio_odr) = val;

	return EOK;
}


int _stm32_gpioGet(unsigned int d, u8 pin, u8 *val)
{
	volatile u32 *base;

	if (d > pctl_gpioh || pin > 15)
		return -EINVAL;

	base = stm32_common.gpio[d - pctl_gpioa];
	*val = !!(*(base + gpio_idr) & (1 << pin));

	return EOK;
}


int _stm32_gpioGetPort(unsigned int d, u16 *val)
{
	volatile u32 *base;

	if (d > pctl_gpioh)
		return -EINVAL;

	base = stm32_common.gpio[d - pctl_gpioa];
	*val = *(base + gpio_idr);

	return EOK;
}


/* CPU info */


unsigned int _stm32_cpuid(void)
{
	return *(stm32_common.scb + scb_cpuid);
}


/* Watchdog */


void _stm32_wdgReload(void)
{
#if defined(WATCHDOG) && defined(NDEBUG)
	*(stm32_common.iwdg + iwdg_kr) = 0xaaaa;
#endif
}


void _stm32_init(void)
{
	u32 t, i;
	static const int gpio2pctl[] = { pctl_gpioa, pctl_gpiob, pctl_gpioc,
		pctl_gpiod, pctl_gpioe, pctl_gpiof, pctl_gpiog, pctl_gpioh, pctl_gpioi };

	/* Base addresses init */
	stm32_common.rcc = (void *)0x40021000;
	stm32_common.pwr = (void *)0x40007000;
	stm32_common.scb = (void *)0xe000e000;
	stm32_common.rtc = (void *)0x40002800;
	stm32_common.nvic = (void *)0xe000e100;
	stm32_common.exti = (void *)0x40010400;
	stm32_common.syst = (void *)0xe000e010;
	stm32_common.mpu = (void *)0xe000ed90;
	stm32_common.syscfg = (void *)0x40010000;
	stm32_common.iwdg = (void *)0x40003000;
	stm32_common.gpio[0] = (void *)0x48000000; /* GPIOA */
	stm32_common.gpio[1] = (void *)0x48000400; /* GPIOB */
	stm32_common.gpio[2] = (void *)0x48000800; /* GPIOC */
	stm32_common.gpio[3] = (void *)0x48000c00; /* GPIOD */
	stm32_common.gpio[4] = (void *)0x48001000; /* GPIOE */
	stm32_common.gpio[5] = (void *)0x48001400; /* GPIOF */
	stm32_common.gpio[6] = (void *)0x48001800; /* GPIOG */
	stm32_common.gpio[7] = (void *)0x48001c00; /* GPIOH */
	stm32_common.gpio[8] = (void *)0x48002000; /* GPIOI */

	/* Store reset flags and then clean them */
	_stm32_rtcUnlockRegs();
	stm32_common.resetFlags = (*(stm32_common.rcc + rcc_csr) >> 26);
	*(stm32_common.rcc + rcc_csr) |= 1 << 24;
	_stm32_rtcLockRegs();

	_stm32_rccSetCPUClock(4 * 1000 * 1000);

	/* Enable System configuration controller */
	_stm32_rccSetDevClock(pctl_syscfg, 1);

	/* Enable power module */
	_stm32_rccSetDevClock(pctl_pwr, 1);

	/* Disable all interrupts */
	*(stm32_common.rcc + rcc_cier) = 0;

	hal_cpuDataBarrier();

	/* Rescue */
	_stm32_rccSetDevClock(pctl_gpiod, 1);
	_stm32_gpioConfig(pctl_gpiod, 1, 0, 0, 0, 0, 1);
	u8 val;
	_stm32_gpioGet(pctl_gpiod, 1, &val);

	while (!val)
		_stm32_gpioGet(pctl_gpiod, 1, &val);

	_stm32_rccSetDevClock(pctl_gpiod, 0);

	/* GPIO LP init */
#ifdef NDEBUG
	i = 0;
#else
	/* Don't change setting for debug pins (needed for JTAG) */
	/* Turn off for production to reduce power consumption */
	_stm32_rccSetDevClock(pctl_gpioa, 1);
	*(stm32_common.gpio[0] + gpio_moder) = 0xabffffff;
	_stm32_rccSetDevClock(pctl_gpioa, 0);
	_stm32_rccSetDevClock(pctl_gpiob, 1);
	*(stm32_common.gpio[1] + gpio_moder) = 0xfffffebf;
	_stm32_rccSetDevClock(pctl_gpiob, 0);

	/* Enable debug in stop mode */
	*((u32*)0xe0042004) |= 3;

	i = 2;
#endif

	/* Init all GPIOs to Ain mode to lower power consumption */
	for (; i <= pctl_gpiog - pctl_gpioa; ++i) {
		_stm32_rccSetDevClock(gpio2pctl[i], 1);
		*(stm32_common.gpio[i] + gpio_moder) = 0xffffffff;
		*(stm32_common.gpio[i] + gpio_pupdr) = 0;
		_stm32_rccSetDevClock(gpio2pctl[i], 0);
	}

	/* Set the internal regulator output voltage to 1.5V */
	_stm32_pwrSetCPUVolt(2);

	/* Set DBP bit */
	*(stm32_common.pwr + pwr_cr1) |= 1 << 8;

	/* Enable LSE clock source */
	*(stm32_common.rcc + rcc_bdcr) |= 1;

	hal_cpuDataBarrier();

	/* And wait for it to turn on */
	while (!(*(stm32_common.rcc + rcc_bdcr) & (1 << 1)));

	*(stm32_common.rcc + rcc_bdcr) |= 1 << 25;

	/* Initialize RTC */

	/* Select LSE as clock source for RTC and LCD */
	*(stm32_common.rcc + rcc_bdcr) = (*(stm32_common.rcc + rcc_bdcr) & ~(0x3 << 8)) | (1 << 8);

	hal_cpuDataBarrier();

	/* Set DBP bit */
	*(stm32_common.pwr + pwr_cr1) |= 1 << 8;

	/* Unlock RTC */
	_stm32_rtcUnlockRegs();

	/* Turn on RTC */
	_stm32_rccSetDevClock(pctl_rtc, 1);
	*(stm32_common.rcc + rcc_bdcr) |= 1 << 15;

	hal_cpuDataBarrier();

	/* Set INIT bit */
	*(stm32_common.rtc + rtc_isr) |= 1 << 7;
	while (!(*(stm32_common.rtc + rtc_isr) & (1 << 6)));

	/* Set RTC prescaler (it has to be done this way) */
	t = *(stm32_common.rtc + rtc_prer) & ~(0x7f << 16);
	*(stm32_common.rtc + rtc_prer) = t | (0x7f << 16);
	t = *(stm32_common.rtc + rtc_prer) & ~0x7fff;
	*(stm32_common.rtc + rtc_prer) = t | 0xff;

	/* Reset RTC interrupt bits WUTIE & WUTE */
	*(stm32_common.rtc + rtc_cr) &= ~((1 << 14) | (1 << 10));

	/* Turn on shadow register bypass */
	*(stm32_common.rtc + rtc_cr) |= 1 << 5;

	/* Select RTC/16 wakeup clock */
	*(stm32_common.rtc + rtc_cr) &= ~0x7;

	/* Clear INIT bit */
	*(stm32_common.rtc + rtc_isr) &= ~(1 << 7);
	_stm32_rtcLockRegs();

	/* Clear pending interrupts */
	*(stm32_common.exti + exti_pr1) |= 0xffffff;
	*(stm32_common.exti + exti_pr2) |= 0xffffff;

#if defined(WATCHDOG) && defined(NDEBUG)
	/* Init watchdog */
	/* Enable write access to IWDG */
	*(stm32_common.iwdg + iwdg_kr) = 0x5555;

	/* Set prescaler to 256, ~30s interval */
	*(stm32_common.iwdg + iwdg_pr) = 0x06;
	*(stm32_common.iwdg + iwdg_rlr) = 0xfff;

	_stm32_wdgReload();

	/* Enable watchdog */
	*(stm32_common.iwdg + iwdg_kr) = 0xcccc;
#endif

	return;
}