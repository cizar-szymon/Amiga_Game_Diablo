/*
 * libsmackerdec - Smacker video decoder
 * Copyright (C) 2011 Barry Duncan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "BitReader.h"
#include <string.h>
#include <vector>

namespace SmackerCommon {

/* One buffer for all readers: the decoder uses one BitReader at a time
 * (header trees, then per frame the audio chunks and the picture). 8 zero
 * bytes after the block: GetBits reads 4 bytes, and a broken stream reads
 * zeros instead of memory after the buffer. */
static std::vector<uint8_t> blockBuffer;

BitReader::BitReader(SmackerCommon::FileStream &file, uint32_t size)
{
	totalSize = size;
	currentOffset = 0;
	if (blockBuffer.size() < size + 8)
		blockBuffer.resize(size + 8);
	uint8_t *buf = blockBuffer.data();
	int32_t got = size != 0 ? file.ReadBytes(buf, size) : 0;
	if (got < 0)
		got = 0;
	memset(buf + got, 0, size + 8 - got);
	data = buf;
}

BitReader::~BitReader()
{
}

} // close namespace SmackerCommon
