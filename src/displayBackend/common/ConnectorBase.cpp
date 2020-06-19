/*
 *  Base connector class
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (C) 2020 EPAM Systems Inc.
 *
 */

#include <endian.h>
#include <xen/be/Exception.hpp>

#include "ConnectorBase.hpp"
#include "drm_edid.h"

using XenBackend::Exception;
using XenBackend::XenGnttabBuffer;

ConnectorBase::ConnectorBase(domid_t domId, uint32_t width, uint32_t height) :
	mDomId(domId),
	mCfgWidth(width),
	mCfgHeight(height),
	mLog("Connector")
{
}

/*******************************************************************************
 * Protected
 ******************************************************************************/

void ConnectorBase::edidPutBlockCheckSum(uint8_t* edidBlock)
{
	int i{0}, checkSum{0};

	for (; i < XENDISPL_EDID_BLOCK_SIZE - 1; i++)
	{
		checkSum += edidBlock[i];
	}

	edidBlock[i] = 0x100 - static_cast<uint8_t>(checkSum);
}

void ConnectorBase::edidPutEssentials(edid* edidBlock)
{
	const uint8_t edidHeader[] = {
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
	};

	memcpy(&edidBlock->header, edidHeader, sizeof(edidHeader));

	/* 16-bit value made up of three 5-bit letters, big-endian. */
	uint16_t vendorId = htobe16((((('X' - '@') & 0x1f) << 10) |
						 ((('E' - '@') & 0x1f) <<  5) |
						 ((('N' - '@') & 0x1f) <<  0)));
	edidBlock->mfg_id[0] = static_cast<uint8_t>(vendorId & 0xff);
	edidBlock->mfg_id[1] = static_cast<uint8_t>(vendorId >> 8);

	/* Manufacturer product code. 16-bit number, little-endian. */
	uint16_t prodCode = htole16(('V' << 8) | 'M');
	edidBlock->prod_code[0] = static_cast<uint8_t>(prodCode & 0xff);
	edidBlock->prod_code[1] = static_cast<uint8_t>(prodCode >> 8);

	/* Serial number. 32 bits, little-endian. */
	edidBlock->serial = htole32(0x20200505);

	/* Week of manufacture - not consistent between manufacturers. */
	edidBlock->mfg_week = 1;
	/* Year of manufacture. Year = datavalue + 1990. */
	edidBlock->mfg_year = EDID_MANUFACTURING_YEAR - 1990;

	/* EDID version and revision: advertise 1.4. */
	edidBlock->version = 1;
	edidBlock->revision = 4;

	/*
	 * Video input parameters bitmap: digital, 8 bits per color,
	 * display port.
	 */
	edidBlock->input = DRM_EDID_INPUT_DIGITAL | DRM_EDID_DIGITAL_DEPTH_8 |
		DRM_EDID_DIGITAL_TYPE_DP;

	/***************************************************************************
	 * TODO: read the below values from the compositor or DRM subsystem.
	 **************************************************************************/

	/* Set width and hight as undefined. */
	edidBlock->width_cm = 0;
	edidBlock->height_cm = 0;

	/* Display gamma, 2.2. */
	edidBlock->gamma = 220 - 100;

	/* Supported features bitmap. */
	edidBlock->features = DRM_EDID_FEATURE_PREFERRED_TIMING |
		DRM_EDID_FEATURE_STANDARD_COLOR;
}

static uint32_t edid_to_10bit(float value)
{
	return (uint32_t)(value * 1024 + 0.5);
}

void ConnectorBase::edidPutColorSpace(edid* edidBlock)
{
	/***************************************************************************
	 * TODO: read the below values from the compositor or DRM subsystem.
	 **************************************************************************/
	const float rx = 0.6400;
	const float ry = 0.3300;
	const float gx = 0.3000;
	const float gy = 0.6000;
	const float bx = 0.1500;
	const float by = 0.0600;
	const float wx = 0.3127;
	const float wy = 0.3290;

	uint32_t red_x   = edid_to_10bit(rx);
	uint32_t red_y   = edid_to_10bit(ry);
	uint32_t green_x = edid_to_10bit(gx);
	uint32_t green_y = edid_to_10bit(gy);
	uint32_t blue_x  = edid_to_10bit(bx);
	uint32_t blue_y  = edid_to_10bit(by);
	uint32_t white_x = edid_to_10bit(wx);
	uint32_t white_y = edid_to_10bit(wy);

	edidBlock->red_green_lo = (((red_x   & 0x03) << 6) |
							   ((red_y   & 0x03) << 4) |
							   ((green_x & 0x03) << 2) |
							   ((green_y & 0x03) << 0));

	edidBlock->black_white_lo = (((blue_x  & 0x03) << 6) |
								 ((blue_y  & 0x03) << 4) |
								 ((white_x & 0x03) << 2) |
								 ((white_y & 0x03) << 0));

	edidBlock->red_x = red_x >> 2;
	edidBlock->red_y = red_y >> 2;

	edidBlock->green_x = green_x >> 2;
	edidBlock->green_y = green_y >> 2;

	edidBlock->blue_x = blue_x >> 2;
	edidBlock->blue_y = blue_y >> 2;

	edidBlock->white_x = white_x >> 2;
	edidBlock->white_y = white_y >> 2;
}

void ConnectorBase::edidPutDetailedTiming(edid* edidBlock, int index,
										  uint32_t xres, uint32_t yres,
										  uint32_t dpi)
{
	detailed_timing* desc = &edidBlock->detailed_timings[index];
	detailed_pixel_timing* pixelData = &desc->data.pixel_data;

	/***************************************************************************
	 * TODO: read the below values from the compositor or DRM subsystem.
	 **************************************************************************/

	/* Physical display size. */
	uint32_t xmm = xres * dpi / 254;
	uint32_t ymm = yres * dpi / 254;

	/* Pull some realistic looking timings out of thin air. */
	uint32_t xfront = xres * 25 / 100;
	uint32_t xsync  = xres *  3 / 100;
	uint32_t xblank = xres * 35 / 100;

	uint32_t yfront = yres *  5 / 1000;
	uint32_t ysync  = yres *  5 / 1000;
	uint32_t yblank = yres * 35 / 1000;

	uint32_t clock = EDID_REFRESH_RATE_HZ * (xres + xblank) * (yres + yblank);

	/* 10 KHz granularity, little endian. */
	desc->pixel_clock = htole32(clock / 10000);

	pixelData->hactive_lo = xres & 0xff;
	pixelData->hblank_lo = xblank & 0xff;
	pixelData->hactive_hblank_hi = (((xres   & 0xf00) >> 4) |
									((xblank & 0xf00) >> 8));

	pixelData->vactive_lo = yres & 0xff;
	pixelData->vblank_lo = yblank & 0xff;
	pixelData->vactive_vblank_hi = (((yres   & 0xf00) >> 4) |
									((yblank & 0xf00) >> 8));

	pixelData->hsync_offset_lo = xfront & 0xff;
	pixelData->hsync_pulse_width_lo = xsync & 0xff;

	pixelData->vsync_offset_pulse_width_lo = (((yfront & 0x00f) << 4) |
											  ((ysync  & 0x00f) << 0));
	pixelData->hsync_vsync_offset_pulse_width_hi = (((xfront & 0x300) >> 2) |
													((xsync  & 0x300) >> 4) |
													((yfront & 0x030) >> 2) |
													((ysync  & 0x030) >> 4));

	pixelData->width_mm_lo = xmm & 0xff;
	pixelData->height_mm_lo = ymm & 0xff;
	pixelData->width_height_mm_hi = (((xmm & 0xf00) >> 4) |
									 ((ymm & 0xf00) >> 8));

	pixelData->hborder = 0;
	pixelData->vborder = 0;

	pixelData->misc = DRM_EDID_PT_SEPARATE_SYNC;
}

void ConnectorBase::edidPutTimings(edid* edidBlock, uint32_t xres, uint32_t yres)
{
	/*
	 * Established timing bitmap. Supported bitmap for (formerly)
	 * very common timing modes: we do not want to provide any.
	 */
	memset(&edidBlock->established_timings, 0,
		   sizeof(edidBlock->established_timings));

	/*
	 * Standard timing information. Up to 8 2-byte fields describing
	 * standard display modes. Unused fields are filled with 01.
	 *
	 * We do not provide any, but detailed timings.
	 */
	memset(edidBlock->standard_timings, 0x01,
		   sizeof(edidBlock->standard_timings));

	/*
	 * Detailed timing descriptors, in decreasing preference order,
	 * followed by Display descriptors.
	 *
	 * We only provide a single timing here which corresponds to
	 * XenStore configuration of this connector.
	 */
	edidPutDetailedTiming(edidBlock, 0, xres, yres, EDID_DPI);
}

size_t ConnectorBase::getEDID(grant_ref_t startDirectory, uint32_t size) const
{
	GrantRefs refs;

	pgDirGetBufferRefs(mDomId, startDirectory, size, refs);
	if (!refs.size())
	{
		throw Exception("Cannot get grant references of the EDID buffer",
						EINVAL);
	}

	XenGnttabBuffer edidBuffer(mDomId, refs.data(), refs.size());

	memset(edidBuffer.get(), 0, XENDISPL_EDID_BLOCK_SIZE);

	auto edidBlock = static_cast<edid*>(edidBuffer.get());

	edidPutEssentials(edidBlock);
	edidPutColorSpace(edidBlock);
	edidPutTimings(edidBlock, mCfgWidth, mCfgHeight);
	edidPutBlockCheckSum(static_cast<uint8_t*>(edidBuffer.get()));

	return XENDISPL_EDID_BLOCK_SIZE;
}

