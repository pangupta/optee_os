// SPDX-License-Identifier: (BSD-2-Clause AND BSD-3-Clause)
/*
 * Copyright (c) 2015, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
 /*
 * Copyright (c) 2014, ARM Limited and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <drivers/tzc400.h>
#include <io.h>
#include <kernel/panic.h>
#include <stddef.h>
#include <trace.h>
#include <util.h>

/*
 * Implementation defined values used to validate inputs later.
 * Filters : max of 4 ; 0 to 3
 * Regions : max of 9 ; 0 to 8
 * Address width : Values between 32 to 64
 */
struct tzc_instance {
	vaddr_t base;
	uint8_t addr_width;
	uint8_t num_filters;
	uint8_t num_regions;
};

static struct tzc_instance tzc;


static uint32_t tzc_read_build_config(vaddr_t base)
{
	return io_read32(base + BUILD_CONFIG_OFF);
}

static uint32_t tzc_read_gate_keeper(vaddr_t base)
{
	return io_read32(base + GATE_KEEPER_OFF);
}

static void tzc_write_gate_keeper(vaddr_t base, uint32_t val)
{
	io_write32(base + GATE_KEEPER_OFF, val);
}

static void tzc_write_action(vaddr_t base, enum tzc_action action)
{
	io_write32(base + ACTION_OFF, action);
}

static void tzc_write_region_base_low(vaddr_t base, uint32_t region,
				      uint32_t val)
{
	io_write32(base + REGION_BASE_LOW_OFF + REGION_NUM_OFF(region), val);
}

static void tzc_write_region_base_high(vaddr_t base, uint32_t region,
				       uint32_t val)
{
	io_write32(base + REGION_BASE_HIGH_OFF + REGION_NUM_OFF(region), val);
}

static void tzc_write_region_top_low(vaddr_t base, uint32_t region,
				     uint32_t val)
{
	io_write32(base + REGION_TOP_LOW_OFF + REGION_NUM_OFF(region), val);
}

static void tzc_write_region_top_high(vaddr_t base, uint32_t region,
				      uint32_t val)
{
	io_write32(base + REGION_TOP_HIGH_OFF +	REGION_NUM_OFF(region), val);
}

static void tzc_write_region_attributes(vaddr_t base, uint32_t region,
					uint32_t val)
{
	io_write32(base + REGION_ATTRIBUTES_OFF + REGION_NUM_OFF(region), val);
}

static void tzc_write_region_id_access(vaddr_t base, uint32_t region,
				       uint32_t val)
{
	io_write32(base + REGION_ID_ACCESS_OFF + REGION_NUM_OFF(region), val);
}

static uint32_t tzc_read_component_id(vaddr_t base)
{
	uint32_t id;

	id = io_read8(base + CID0_OFF);
	id |= SHIFT_U32(io_read8(base + CID1_OFF), 8);
	id |= SHIFT_U32(io_read8(base + CID2_OFF), 16);
	id |= SHIFT_U32(io_read8(base + CID3_OFF), 24);

	return id;
}

static uint32_t tzc_get_gate_keeper(vaddr_t base, uint8_t filter)
{
	uint32_t tmp;

	tmp = (tzc_read_gate_keeper(base) >> GATE_KEEPER_OS_SHIFT) &
		GATE_KEEPER_OS_MASK;

	return (tmp >> filter) & GATE_KEEPER_FILTER_MASK;
}

/* This function is not MP safe. */
static void tzc_set_gate_keeper(vaddr_t base, uint8_t filter, uint32_t val)
{
	uint32_t tmp;

	/* Upper half is current state. Lower half is requested state. */
	tmp = (tzc_read_gate_keeper(base) >> GATE_KEEPER_OS_SHIFT) &
		GATE_KEEPER_OS_MASK;

	if (val)
		tmp |=  (1 << filter);
	else
		tmp &= ~(1 << filter);

	tzc_write_gate_keeper(base, (tmp & GATE_KEEPER_OR_MASK) <<
			      GATE_KEEPER_OR_SHIFT);

	/* Wait here until we see the change reflected in the TZC status. */
	while (((tzc_read_gate_keeper(base) >> GATE_KEEPER_OS_SHIFT) &
		GATE_KEEPER_OS_MASK) != tmp)
		;
}


void tzc_init(vaddr_t base)
{
	uint32_t tzc_id, tzc_build;

	assert(base);
	tzc.base = base;

	/*
	 * We expect to see a tzc400. Check component ID. The TZC-400 TRM shows
	 * component ID is expected to be "0xB105F00D".
	 */
	tzc_id = tzc_read_component_id(tzc.base);
	if (tzc_id != TZC400_COMPONENT_ID) {
		EMSG("TZC : Wrong device ID (0x%x).\n", tzc_id);
		panic();
	}

	/* Save values we will use later. */
	tzc_build = tzc_read_build_config(tzc.base);
	tzc.num_filters = ((tzc_build >> BUILD_CONFIG_NF_SHIFT) &
			   BUILD_CONFIG_NF_MASK) + 1;
	tzc.addr_width  = ((tzc_build >> BUILD_CONFIG_AW_SHIFT) &
			   BUILD_CONFIG_AW_MASK) + 1;
	tzc.num_regions = ((tzc_build >> BUILD_CONFIG_NR_SHIFT) &
			   BUILD_CONFIG_NR_MASK) + 1;
}

static uint32_t addr_low(vaddr_t addr)
{
	return (uint32_t)addr;
}

static uint32_t addr_high(vaddr_t addr __unused)
{
#if (UINTPTR_MAX == UINT64_MAX)
	return (addr >> 32);
#else
	return 0;
#endif
}


/*
 * `tzc_configure_region` is used to program regions into the TrustZone
 * controller. A region can be associated with more than one filter. The
 * associated filters are passed in as a bitmap (bit0 = filter0).
 * NOTE:
 * The region 0 covers the whole address space and is enabled on all filters,
 * this cannot be changed. It is, however, possible to change some region 0
 * permissions.
 */
void tzc_configure_region(uint32_t filters,
			  uint8_t  region,
			  vaddr_t  region_base,
			  vaddr_t  region_top,
			  enum tzc_region_attributes sec_attr,
			  uint32_t ns_device_access)
{
	assert(tzc.base);

	/* Do range checks on filters and regions. */
	assert(((filters >> tzc.num_filters) == 0) &&
	       (region < tzc.num_regions));

	/*
	 * Do address range check based on TZC configuration. A 64bit address is
	 * the max and expected case.
	 */
#if (UINTPTR_MAX == UINT64_MAX)
	assert(((region_top <= (UINT64_MAX >> (64 - tzc.addr_width))) &&
		(region_base < region_top)));
#endif
	/* region_base and (region_top + 1) must be 4KB aligned */
	assert(((region_base | (region_top + 1)) & (4096 - 1)) == 0);

	assert(sec_attr <= TZC_REGION_S_RDWR);

	/*
	 * Inputs look ok, start programming registers.
	 * All the address registers are 32 bits wide and have a LOW and HIGH
	 * component used to construct a up to a 64bit address.
	 */
	tzc_write_region_base_low(tzc.base, region,
				  addr_low(region_base));
	tzc_write_region_base_high(tzc.base, region,
				   addr_high(region_base));

	tzc_write_region_top_low(tzc.base, region,
				addr_low(region_top));
	tzc_write_region_top_high(tzc.base, region,
				addr_high(region_top));

	/* Assign the region to a filter and set secure attributes */
	tzc_write_region_attributes(tzc.base, region,
		(sec_attr << REG_ATTR_SEC_SHIFT) | filters);

	/*
	 * Specify which non-secure devices have permission to access this
	 * region.
	 */
	tzc_write_region_id_access(tzc.base, region, ns_device_access);
}


void tzc_set_action(enum tzc_action action)
{
	assert(tzc.base);

	/*
	 * - Currently no handler is provided to trap an error via interrupt
	 *   or exception.
	 * - The interrupt action has not been tested.
	 */
	tzc_write_action(tzc.base, action);
}


void tzc_enable_filters(void)
{
	uint32_t state;
	uint32_t filter;

	assert(tzc.base);

	for (filter = 0; filter < tzc.num_filters; filter++) {
		state = tzc_get_gate_keeper(tzc.base, filter);
		if (state) {
			/*
			 * The TZC filter is already configured. Changing the
			 * programmer's view in an active system can cause
			 * unpredictable behavior therefore panic for now rather
			 * than try to determine whether this is safe in this
			 * instance. See:
			 * http://infocenter.arm.com/help/index.jsp?\
			 * topic=/com.arm.doc.ddi0504c/CJHHECBF.html
			 */
			EMSG("TZC : Filter %d Gatekeeper already enabled.\n",
				filter);
			panic();
		}
		tzc_set_gate_keeper(tzc.base, filter, 1);
	}
}


void tzc_disable_filters(void)
{
	uint32_t filter;

	assert(tzc.base);

	/*
	 * We don't do the same state check as above as the Gatekeepers are
	 * disabled after reset.
	 */
	for (filter = 0; filter < tzc.num_filters; filter++)
		tzc_set_gate_keeper(tzc.base, filter, 0);
}

#if TRACE_LEVEL >= TRACE_DEBUG

static uint32_t tzc_read_region_attributes(vaddr_t base, uint32_t region)
{
	return io_read32(base + REGION_ATTRIBUTES_OFF + REGION_NUM_OFF(region));
}

static uint32_t tzc_read_region_base_low(vaddr_t base, uint32_t region)
{
	return io_read32(base + REGION_BASE_LOW_OFF + REGION_NUM_OFF(region));
}

static uint32_t tzc_read_region_base_high(vaddr_t base, uint32_t region)
{
	return io_read32(base + REGION_BASE_HIGH_OFF + REGION_NUM_OFF(region));
}

static uint32_t tzc_read_region_top_low(vaddr_t base, uint32_t region)
{
	return io_read32(base + REGION_TOP_LOW_OFF + REGION_NUM_OFF(region));
}

static uint32_t tzc_read_region_top_high(vaddr_t base, uint32_t region)
{
	return io_read32(base + REGION_TOP_HIGH_OFF + REGION_NUM_OFF(region));
}

#define	REGION_MAX		8
static const __maybe_unused char * const tzc_attr_msg[] = {
	"TZC_REGION_S_NONE",
	"TZC_REGION_S_RD",
	"TZC_REGION_S_WR",
	"TZC_REGION_S_RDWR"
};

void tzc_dump_state(void)
{
	uint32_t n;
	uint32_t temp_32reg, temp_32reg_h;

	DMSG("enter");
	for (n = 0; n <= REGION_MAX; n++) {
		temp_32reg = tzc_read_region_attributes(tzc.base, n);
		if (!(temp_32reg & REG_ATTR_F_EN_MASK))
			continue;

		DMSG("\n");
		DMSG("region %d", n);
		temp_32reg = tzc_read_region_base_low(tzc.base, n);
		temp_32reg_h = tzc_read_region_base_high(tzc.base, n);
		DMSG("region_base: 0x%08x%08x", temp_32reg_h, temp_32reg);
		temp_32reg = tzc_read_region_top_low(tzc.base, n);
		temp_32reg_h = tzc_read_region_top_high(tzc.base, n);
		DMSG("region_top: 0x%08x%08x", temp_32reg_h, temp_32reg);
		temp_32reg = tzc_read_region_attributes(tzc.base, n);
		DMSG("secure rw: %s",
		     tzc_attr_msg[temp_32reg >> REG_ATTR_SEC_SHIFT]);
		if (temp_32reg & (1 << 0))
			DMSG("filter 0 enable");
		if (temp_32reg & (1 << 1))
			DMSG("filter 1 enable");
		if (temp_32reg & (1 << 2))
			DMSG("filter 2 enable");
		if (temp_32reg & (1 << 3))
			DMSG("filter 3 enable");
	}
	DMSG("exit");
}

#endif /* CFG_TRACE_LEVEL >= TRACE_DEBUG */
